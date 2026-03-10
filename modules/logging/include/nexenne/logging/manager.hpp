#pragma once

/**
 * @file
 * @brief Backend that drains records onto sinks (async-threaded or synchronous).
 *
 * Two concrete backends share one public API so user code stays config-agnostic:
 *
 *   - \c basic_async_manager<Config> : owns a lock-free MPMC queue
 *     (\c container::mpmc_queue) and a backend thread. Producers push and return
 *     immediately; the backend thread dispatches to every registered sink. A
 *     full queue drops the record and bumps \c dropped_count.
 *   - \c basic_sync_manager<Config> : no thread, no queue. Every \c push
 *     dispatches on the calling thread. Right for init-time logging, a
 *     single-core MCU, and deterministic-ordering tests.
 *
 * \c basic_manager<Config> resolves to the right one from \c Config::async.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/mpmc_queue.hpp>
#include <nexenne/logging/config.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace nexenne::logging {

/**
 * @brief Asynchronous logging backend: a lock-free queue plus a backend thread.
 *
 * A process-wide singleton per \p Config. Producers \c push records into an MPMC
 * queue and return immediately; the owned backend thread drains the queue and
 * dispatches each record to every registered sink. A full queue drops the record
 * and bumps \c dropped_count.
 *
 * @tparam Config Configuration policy with \c async true and a power-of-two
 *         \c queue_size of at least two.
 *
 * @pre \p Config::async is \c true and \p Config::queue_size is a power of two
 *      no smaller than two.
 * @post None.
 */
template <config_like Config>
  requires(Config::async)
          && ((Config::queue_size & (Config::queue_size - 1)) == 0 && Config::queue_size >= 2)
class basic_async_manager {
public:
  using config_type = Config;
  using size_type = std::size_t;

  static constexpr size_type queue_size = Config::queue_size;
  static constexpr bool is_async = true;

private:
  basic_async_manager() noexcept {
    m_backend = std::thread{[this] { backend_loop(); }};
  }

  ~basic_async_manager() noexcept {
    shutdown();
  }

  auto backend_loop() noexcept -> void {
    while (!m_stop.load(std::memory_order_acquire)) {
      auto const token{m_signal.load(std::memory_order_acquire)};
      drain_queue();
      if (m_stop.load(std::memory_order_acquire)) {
        break;
      }
      // Block until a producer (or shutdown) bumps m_signal. If one already did
      // between the token snapshot and here, wait returns at once, so there is no
      // lost wakeup; a genuinely idle backend blocks indefinitely with no poll,
      // so the CPU can reach deep sleep on an embedded target.
      m_signal.wait(token, std::memory_order_acquire);
    }
    drain_queue();
  }

  auto drain_queue() noexcept -> void {
    m_dispatching.store(true, std::memory_order_release);
    while (auto popped{m_queue.try_pop()}) {
      dispatch(*popped);
    }
    m_dispatching.store(false, std::memory_order_release);
  }

  auto dispatch(record const& r) noexcept -> void {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    for (auto& s : m_sinks) {
      if (s != nullptr) {
        s->write(r);
      }
    }
  }

  container::mpmc_queue<record, Config::queue_size> m_queue{};
  std::atomic<size_type> m_dropped{0};
  std::atomic<bool> m_stop{false};
  std::atomic<bool> m_dispatching{false};
  // Monotonic wakeup token: bumped on every successful push and on shutdown so
  // the backend's atomic wait returns. Pairs with m_signal.notify_one().
  std::atomic<std::uint32_t> m_signal{0};
  std::thread m_backend;

  mutable std::mutex m_sinks_mutex;
  std::vector<std::shared_ptr<sink>> m_sinks;

public:
  /**
   * @brief Access the process-wide singleton for \p Config.
   *
   * The first call constructs the manager and starts its backend thread.
   *
   * @return A reference to the shared async manager.
   *
   * @pre None.
   * @post The backend thread is running.
   */
  [[nodiscard]] static auto instance() noexcept -> basic_async_manager& {
    static basic_async_manager s_instance{};
    return s_instance;
  }

  basic_async_manager(basic_async_manager const&) = delete;
  auto operator=(basic_async_manager const&) -> basic_async_manager& = delete;
  basic_async_manager(basic_async_manager&&) = delete;
  auto operator=(basic_async_manager&&) -> basic_async_manager& = delete;

  /**
   * @brief Registers a sink to receive every dispatched record.
   *
   * Not \c noexcept: growing the sink list may allocate.
   *
   * @param s Sink to add; ownership is shared.
   *
   * @pre None.
   * @post \c sink_count() has grown by one.
   * @throws std::bad_alloc if the sink list cannot grow.
   */
  auto add_sink(std::shared_ptr<sink> s) -> void {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    m_sinks.push_back(std::move(s));
  }

  /**
   * @brief Removes all registered sinks.
   *
   * @pre None.
   * @post \c sink_count() is zero.
   */
  auto clear_sinks() noexcept -> void {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    m_sinks.clear();
  }

  /**
   * @brief Number of registered sinks.
   *
   * @return The current sink count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto sink_count() const noexcept -> size_type {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    return m_sinks.size();
  }

  /**
   * @brief Enqueues a record for the backend thread, non-blocking.
   *
   * Wakes the backend on success; on a full queue the record is dropped and
   * \c dropped_count grows.
   *
   * @param r Record to enqueue, moved in.
   *
   * @return An empty \c std::expected on success, or a container error when the
   *         queue is full.
   *
   * @pre None.
   * @post On failure, \c dropped_count() has grown by one.
   *
   * @complexity \c O(1).
   */
  auto push(record r) noexcept -> std::expected<void, container::container_error> {
    auto result{m_queue.push(std::move(r))};
    if (result.has_value()) {
      wake_backend();
    } else {
      m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
  }

  /**
   * @brief Enqueues a record, spinning until there is room.
   *
   * Yields between attempts; never drops the record.
   *
   * @param r Record to enqueue, moved in.
   *
   * @pre None.
   * @post The record has been enqueued for the backend thread.
   */
  auto push_blocking(record r) noexcept -> void {
    while (true) {
      auto result{m_queue.push(std::move(r))};
      if (result.has_value()) {
        wake_backend();
        return;
      }
      std::this_thread::yield();
    }
  }

  /**
   * @brief Number of records dropped due to a full queue.
   *
   * @return The cumulative dropped-record count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto dropped_count() const noexcept -> size_type {
    return m_dropped.load(std::memory_order_relaxed);
  }

  /**
   * @brief Blocks until queued records are dispatched, then flushes sinks.
   *
   * Waits for the queue to drain and for the backend to finish dispatching its
   * current batch (the \c m_dispatching flag closes the pop-but-not-yet-written
   * window) before flushing each sink. Guaranteed for records pushed by the
   * calling thread before this call; a record pushed concurrently by another
   * thread is best-effort (the queue's emptiness check is relaxed).
   *
   * @pre None.
   * @post Every record the calling thread enqueued before this call has been
   *       dispatched and every sink has been flushed.
   */
  auto flush() noexcept -> void {
    while (!m_queue.empty_approx() || m_dispatching.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    auto const guard{std::lock_guard{m_sinks_mutex}};
    for (auto& s : m_sinks) {
      if (s != nullptr) {
        s->flush();
      }
    }
  }

  /**
   * @brief Stops the backend thread, draining and flushing once.
   *
   * Idempotent: a second call returns immediately. Called automatically at
   * destruction.
   *
   * @pre None.
   * @post The backend thread has stopped, the queue is drained, and every sink
   *       has been flushed.
   */
  auto shutdown() noexcept -> void {
    if (m_stop.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    wake_backend();
    if (m_backend.joinable()) {
      m_backend.join();
    }
    drain_queue();
    auto const guard{std::lock_guard{m_sinks_mutex}};
    for (auto& s : m_sinks) {
      if (s != nullptr) {
        s->flush();
      }
    }
  }

private:
  // Bumps the wakeup token and wakes the backend if it is parked in wait().
  // notify_one skips the kernel wake when no thread is waiting, so a push that
  // arrives while the backend is busy draining costs only the atomic bump.
  auto wake_backend() noexcept -> void {
    m_signal.fetch_add(1, std::memory_order_release);
    m_signal.notify_one();
  }
};

/**
 * @brief Synchronous logging backend: no thread, no queue.
 *
 * A process-wide singleton per \p Config. Every \c push dispatches the record to
 * each registered sink on the calling thread. Right for init-time logging, a
 * single-core MCU, and deterministic-ordering tests.
 *
 * @tparam Config Configuration policy with \c async false.
 *
 * @pre \p Config::async is \c false.
 * @post None.
 */
template <config_like Config>
  requires(!Config::async)
class basic_sync_manager {
public:
  using config_type = Config;
  using size_type = std::size_t;

  static constexpr size_type queue_size = 0;
  static constexpr bool is_async = false;

private:
  basic_sync_manager() noexcept = default;
  ~basic_sync_manager() noexcept = default;

  auto dispatch(record const& r) noexcept -> void {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    for (auto& s : m_sinks) {
      if (s != nullptr) {
        s->write(r);
      }
    }
  }

  mutable std::mutex m_sinks_mutex;
  std::vector<std::shared_ptr<sink>> m_sinks;

public:
  /**
   * @brief Access the process-wide singleton for \p Config.
   *
   * @return A reference to the shared sync manager.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static auto instance() noexcept -> basic_sync_manager& {
    static basic_sync_manager s_instance{};
    return s_instance;
  }

  basic_sync_manager(basic_sync_manager const&) = delete;
  auto operator=(basic_sync_manager const&) -> basic_sync_manager& = delete;
  basic_sync_manager(basic_sync_manager&&) = delete;
  auto operator=(basic_sync_manager&&) -> basic_sync_manager& = delete;

  /**
   * @brief Registers a sink to receive every dispatched record.
   *
   * Not \c noexcept: growing the sink list may allocate.
   *
   * @param s Sink to add; ownership is shared.
   *
   * @pre None.
   * @post \c sink_count() has grown by one.
   * @throws std::bad_alloc if the sink list cannot grow.
   */
  auto add_sink(std::shared_ptr<sink> s) -> void {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    m_sinks.push_back(std::move(s));
  }

  /**
   * @brief Removes all registered sinks.
   *
   * @pre None.
   * @post \c sink_count() is zero.
   */
  auto clear_sinks() noexcept -> void {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    m_sinks.clear();
  }

  /**
   * @brief Number of registered sinks.
   *
   * @return The current sink count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto sink_count() const noexcept -> size_type {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    return m_sinks.size();
  }

  /**
   * @brief Dispatches a record to every sink on the calling thread.
   *
   * Always succeeds (no queue to overflow); the \c std::expected return keeps
   * the signature uniform with the async manager.
   *
   * @param r Record to dispatch, moved in.
   *
   * @return An always-engaged empty \c std::expected.
   *
   * @pre None.
   * @post \p r has been written to every registered sink that accepts it.
   */
  auto push(record r) noexcept -> std::expected<void, container::container_error> {
    dispatch(r);
    return {};
  }

  /**
   * @brief Dispatches a record synchronously; identical to \c push here.
   *
   * @param r Record to dispatch, moved in.
   *
   * @pre None.
   * @post \p r has been written to every registered sink that accepts it.
   */
  auto push_blocking(record r) noexcept -> void {
    dispatch(r);
  }

  /**
   * @brief Number of dropped records, always zero in sync mode.
   *
   * @return Zero, since there is no queue to drop from.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto dropped_count() noexcept -> size_type {
    return 0;
  }

  /**
   * @brief Flushes every registered sink.
   *
   * @pre None.
   * @post Every registered sink has been flushed.
   */
  auto flush() noexcept -> void {
    auto const guard{std::lock_guard{m_sinks_mutex}};
    for (auto& s : m_sinks) {
      if (s != nullptr) {
        s->flush();
      }
    }
  }

  /**
   * @brief Flushes all sinks; a no-op stop provided for API uniformity.
   *
   * @pre None.
   * @post Every registered sink has been flushed.
   */
  auto shutdown() noexcept -> void {
    flush();
  }
};

namespace detail {

// A class-level requires clause is checked when the template-id is FORMED, not
// only when the class template is instantiated. Naming both
// basic_async_manager<Config> and basic_sync_manager<Config> (as std::conditional
// would) would fail the non-matching branch's constraint at formation, so this
// selector forms only the selected specialisation's template-id.
template <config_like Config, bool Async = Config::async>
struct manager_selector {
  using type = basic_async_manager<Config>;
};

template <config_like Config>
struct manager_selector<Config, false> {
  using type = basic_sync_manager<Config>;
};

}  // namespace detail

/**
 * @brief Selector alias picking the concrete manager from \p Config.
 *
 * Resolves to \c basic_async_manager when \c Config::async is \c true and to
 * \c basic_sync_manager otherwise. Both expose the same public API.
 *
 * @tparam Config Configuration policy satisfying \c config_like.
 *
 * @pre None.
 * @post None.
 */
template <config_like Config>
using basic_manager = typename detail::manager_selector<Config>::type;

/**
 * @brief Default manager type backed by \c default_config.
 *
 * @pre None.
 * @post None.
 */
using manager = basic_manager<default_config>;

}  // namespace nexenne::logging
