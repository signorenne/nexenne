#pragma once

/**
 * @file
 * @brief Decorator sink that offloads writes to a background thread.
 *
 * \c async_sink wraps a "real" sink (file, console, ...) and inserts a
 * thread-safe queue between producer threads and the actual write. Producer
 * threads enqueue cheaply (mutex plus push) and return; a dedicated background
 * thread drains the queue and calls the wrapped sink's \c write.
 *
 * Use it when logging sits on a latency-sensitive hot path that cannot afford
 * synchronous I/O, or when the destination is slow (rotating files, network).
 *
 * The queue is bounded by \c async_sink_config::queue_size_limit. When it is
 * full the configured \c overflow_action decides what happens: \c block stalls
 * the producer until space frees, \c drop_oldest discards the head to make
 * room, and \c drop_newest discards the incoming record.
 *
 * Shutdown semantics: the destructor performs a graceful drain. It signals the
 * worker to stop, the worker writes every record already in the queue, then the
 * thread is joined and the wrapped sink flushed. No queued record is lost on a
 * normal teardown. A producer parked in the \c block policy is released when
 * shutdown begins; the record it was trying to enqueue is dropped, since the
 * queue is closing.
 *
 * Concurrency: producers may call \c write concurrently. The wrapped sink's
 * \c write runs only on the single background thread, matching the serial-write
 * contract the \c sink interface documents. The background thread never lets an
 * exception escape (that would terminate the process via \c std::thread), and
 * the wrapped sink's \c write is itself \c noexcept.
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace nexenne::logging {

/**
 * @brief Action taken when the async queue reaches its size limit.
 *
 * @pre None.
 * @post None.
 */
enum class overflow_action : std::uint8_t {
  block,        ///< Block the producer until space becomes available.
  drop_oldest,  ///< Remove the oldest queued record and push the new one.
  drop_newest,  ///< Silently discard the incoming record.
};

/**
 * @brief Configuration for \c async_sink.
 *
 * @pre None.
 * @post None.
 */
struct async_sink_config {
  std::size_t queue_size_limit{1024};                   ///< Maximum number of queued records.
  overflow_action on_overflow{overflow_action::block};  ///< Action when the queue is full.
};

/**
 * @brief Decorator sink that offloads writes to a dedicated background thread.
 *
 * Wraps an inner sink and buffers incoming records in a mutex-guarded queue.
 * The background thread drains the queue and calls the inner sink's \c write.
 * The destructor drains the queue, joins the background thread, and flushes the
 * inner sink.
 *
 * Why a mutex plus condition variable and not \c container::mpmc_queue: the
 * lock-free queue is correct and used elsewhere in this module, but it does not
 * fit this sink's contract. Its capacity is a compile-time template parameter,
 * whereas \c queue_size_limit is a runtime, per-instance value. The \c block
 * policy needs a real blocking wait (the lock-free queue only offers
 * spin or yield), and \c drop_oldest needs an atomic pop-then-push that the
 * lock-free queue cannot express under concurrent producers. The mutex plus
 * condition variable design supports all three policies and a blocking drain
 * directly, so it is kept here.
 *
 * @pre None.
 * @post On construction the background thread is running and the inner sink is
 *       owned by this object.
 */
class async_sink final : public sink {
public:
  /// Alias for \c async_sink_config.
  using config = async_sink_config;

  /**
   * @brief Constructs an async sink wrapping \p inner.
   *
   * Starts the background worker thread immediately.
   *
   * @param inner Sink that performs the actual writes; must not be null.
   * @param cfg Queue and overflow configuration.
   *
   * @pre \p inner is a valid non-null sink.
   * @post The background thread is running and ready to drain records.
   */
  explicit async_sink(std::unique_ptr<sink> inner, config const cfg = config{})
      : m_inner{std::move(inner)}, m_cfg{cfg} {
    m_worker = std::thread{[this] { run(); }};
  }

  async_sink(async_sink const&) = delete;
  auto operator=(async_sink const&) -> async_sink& = delete;
  async_sink(async_sink&&) = delete;
  auto operator=(async_sink&&) -> async_sink& = delete;

  /**
   * @brief Drains the queue, joins the background thread, and flushes the inner sink.
   *
   * Performs a graceful shutdown: every record already queued is written before
   * the thread stops. Never throws.
   *
   * @pre None.
   * @post All queued records have been dispatched, the background thread has
   *       stopped, and the inner sink has been flushed.
   */
  ~async_sink() noexcept override {
    {
      auto const lk{std::scoped_lock{m_mu}};
      m_stop = true;
    }
    // Wake the worker (it may be waiting for records) and any producer parked
    // on the block policy (it must observe the stop and bail out).
    m_not_empty.notify_all();
    m_not_full.notify_all();
    if (m_worker.joinable()) {
      m_worker.join();
    }
    if (m_inner) {
      m_inner->flush();
    }
  }

protected:
  /**
   * @brief Enqueues \p r for the background thread, applying the overflow policy.
   *
   * Not allocation-free: pushing onto the queue may allocate. \c noexcept all
   * the same, per the \c sink contract; an allocation failure terminates rather
   * than unwinding a producer through the sink interface.
   *
   * @param r Record to enqueue.
   *
   * @pre None.
   * @post On success the record is queued; otherwise it was dropped per
   *       \c on_overflow, or the sink is shutting down.
   *
   * @complexity \c O(1) amortised plus the wait under the \c block policy.
   */
  auto write_out(record const& r) noexcept -> void override {
    auto lk{std::unique_lock{m_mu}};
    if (m_queue.size() >= m_cfg.queue_size_limit) {
      switch (m_cfg.on_overflow) {
        case overflow_action::block:
          m_not_full.wait(lk, [this] { return m_queue.size() < m_cfg.queue_size_limit || m_stop; });
          if (m_stop) {
            return;
          }
          break;
        case overflow_action::drop_oldest:
          m_queue.pop();
          break;
        case overflow_action::drop_newest:
          return;
      }
    }
    if (m_stop) {
      return;
    }
    m_queue.push(r);
    lk.unlock();
    m_not_empty.notify_one();
  }

  /**
   * @brief Blocks until the queue drains, then flushes the inner sink.
   *
   * Best-effort: records enqueued concurrently with the flush may or may not be
   * included. Returns early if a shutdown is in progress.
   *
   * @pre None.
   * @post The queue observed empty at some point and the inner sink was flushed.
   */
  auto flush_out() noexcept -> void override {
    {
      auto lk{std::unique_lock{m_mu}};
      m_drained.wait(lk, [this] { return m_queue.empty() || m_stop; });
    }
    if (m_inner) {
      m_inner->flush();
    }
  }

private:
  /**
   * @brief Background loop: drains the queue and writes to the inner sink.
   *
   * Exits only after the queue is empty and a stop has been requested, so a
   * graceful shutdown loses no queued record.
   *
   * @pre None.
   * @post The queue is empty and a stop was requested.
   */
  auto run() noexcept -> void {
    while (true) {
      auto r{record{}};
      {
        auto lk{std::unique_lock{m_mu}};
        m_not_empty.wait(lk, [this] { return m_stop || !m_queue.empty(); });
        if (m_queue.empty()) {
          // The queue is empty; the predicate only also lets us through on a
          // stop request, so this is the graceful-shutdown exit.
          return;
        }
        r = std::move(m_queue.front());
        m_queue.pop();
        if (m_queue.empty()) {
          // Signal flush_out waiters before releasing the lock so a flush
          // sees the empty queue rather than racing the next push.
          m_drained.notify_all();
        }
      }
      // A slot just freed up; release a producer parked on the block policy.
      m_not_full.notify_one();
      if (m_inner) {
        m_inner->write(r);
      }
    }
  }

  std::unique_ptr<sink> m_inner;
  config m_cfg;
  mutable std::mutex m_mu;
  std::condition_variable m_not_empty;  ///< Worker waits here for records.
  std::condition_variable m_not_full;   ///< Blocking producers wait here for space.
  std::condition_variable m_drained;    ///< Flush waiters wait here for an empty queue.
  std::queue<record> m_queue;
  bool m_stop{false};  ///< Guarded by \c m_mu; set once at shutdown.
  std::thread m_worker;
};

}  // namespace nexenne::logging
