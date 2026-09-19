/**
 * @file
 * @brief Tests for the sync and async manager backends and the selector.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <nexenne/logging/config.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/manager.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace lg = nexenne::logging;

class capture_sink final : public lg::sink {
public:
  [[nodiscard]] auto count() const noexcept -> std::size_t {
    return m_count.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto flushes() const noexcept -> std::size_t {
    return m_flushes.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto messages() const -> std::vector<std::string> {
    auto const guard{std::lock_guard{m_mutex}};
    return m_messages;
  }

protected:
  auto write_out(lg::record const& r) noexcept -> void override {
    {
      auto const guard{std::lock_guard{m_mutex}};
      m_messages.push_back(r.message);
    }
    m_count.fetch_add(1, std::memory_order_release);
  }

  auto flush_out() noexcept -> void override {
    m_flushes.fetch_add(1, std::memory_order_release);
  }

private:
  mutable std::mutex m_mutex;
  std::vector<std::string> m_messages;
  std::atomic<std::size_t> m_count{0};
  std::atomic<std::size_t> m_flushes{0};
};

[[nodiscard]] auto rec(std::string msg) -> lg::record {
  return lg::record{lg::level::info, std::source_location::current(), "t", std::move(msg)};
}

using sync_cfg = lg::config<1, false>;
using async_cfg = lg::config<1024, true>;

TEST_CASE("nexenne::logging sync manager dispatches inline to every registered sink") {
  auto& mgr{lg::basic_manager<sync_cfg>::instance()};
  mgr.clear_sinks();
  CHECK(mgr.sink_count() == 0);

  auto cap{std::make_shared<capture_sink>()};
  mgr.add_sink(cap);
  CHECK(mgr.sink_count() == 1);

  nexenne::utility::ignore(mgr.push(rec("a")));
  nexenne::utility::ignore(mgr.push(rec("b")));
  CHECK(cap->count() == 2);
  CHECK(mgr.dropped_count() == 0);

  mgr.flush();
  CHECK(cap->flushes() >= 1);

  mgr.clear_sinks();
  CHECK(mgr.sink_count() == 0);
}

TEST_CASE("nexenne::logging sync manager push_blocking also dispatches inline") {
  auto& mgr{lg::basic_manager<sync_cfg>::instance()};
  mgr.clear_sinks();
  auto cap{std::make_shared<capture_sink>()};
  mgr.add_sink(cap);

  mgr.push_blocking(rec("x"));
  CHECK(cap->count() == 1);
  CHECK(cap->messages().front() == "x");

  mgr.clear_sinks();
}

TEST_CASE("nexenne::logging async manager drains every pushed record after flush") {
  auto& mgr{lg::basic_manager<async_cfg>::instance()};
  mgr.clear_sinks();
  auto cap{std::make_shared<capture_sink>()};
  mgr.add_sink(cap);

  constexpr std::size_t total{500};
  for (std::size_t i{0}; i < total; ++i) {
    nexenne::utility::ignore(mgr.push(rec(std::to_string(i))));
  }
  mgr.flush();
  CHECK(cap->count() == total);
  CHECK(mgr.dropped_count() == 0);

  mgr.clear_sinks();
}

TEST_CASE("nexenne::logging async manager loses no record under concurrent producers") {
  auto& mgr{lg::basic_manager<async_cfg>::instance()};
  mgr.clear_sinks();
  auto cap{std::make_shared<capture_sink>()};
  mgr.add_sink(cap);

  constexpr std::size_t producers{4};
  constexpr std::size_t per_producer{2000};
  constexpr std::size_t total{producers * per_producer};
  {
    auto threads{std::vector<std::thread>{}};
    for (std::size_t p{0}; p < producers; ++p) {
      threads.emplace_back([&mgr, p] {
        for (std::size_t i{0}; i < per_producer; ++i) {
          mgr.push_blocking(rec(std::to_string(p * per_producer + i)));
        }
      });
    }
    for (auto& t : threads) {
      t.join();
    }
  }
  mgr.flush();
  CHECK(cap->count() == total);
  CHECK(mgr.dropped_count() == 0);

  mgr.clear_sinks();
}

TEST_CASE("nexenne::logging manager selector and aliases resolve from the config") {
  static_assert(lg::basic_manager<sync_cfg>::is_async == false);
  static_assert(lg::basic_manager<async_cfg>::is_async == true);
  static_assert(lg::basic_manager<sync_cfg>::queue_size == 0);
  static_assert(lg::basic_manager<async_cfg>::queue_size == 1024);
  static_assert(std::is_same_v<lg::manager, lg::basic_manager<lg::default_config>>);
  CHECK(true);
}

TEST_CASE("nexenne::logging async manager writes on the calling thread after shutdown") {
  using stopped_cfg = lg::config<4, true>;
  auto& mgr{lg::basic_manager<stopped_cfg>::instance()};
  auto cap{std::make_shared<capture_sink>()};
  mgr.add_sink(cap);
  mgr.shutdown();

  CHECK(mgr.push(rec("late")).has_value());
  CHECK(cap->count() == 1);
  mgr.push_blocking(rec("later"));
  CHECK(cap->count() == 2);
  mgr.flush();
  CHECK(cap->flushes() >= 1);
  CHECK(mgr.dropped_count() == 0);

  mgr.clear_sinks();
}

#if defined(CLOCK_THREAD_CPUTIME_ID)

class slow_sink final : public lg::sink {
public:
  [[nodiscard]] auto count() const noexcept -> std::size_t {
    return m_count.load(std::memory_order_acquire);
  }

protected:
  auto write_out(lg::record const&) noexcept -> void override {
    std::this_thread::sleep_for(std::chrono::milliseconds{25});
    m_count.fetch_add(1, std::memory_order_release);
  }

  auto flush_out() noexcept -> void override {}

private:
  std::atomic<std::size_t> m_count{0};
};

[[nodiscard]] auto thread_cpu_time() -> std::chrono::nanoseconds {
  auto ts{timespec{}};
  nexenne::utility::ignore(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts));
  return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

template <typename Fn>
[[nodiscard]] auto waits_without_spinning(Fn&& fn) -> bool {
  auto const wall_start{std::chrono::steady_clock::now()};
  auto const cpu_start{thread_cpu_time()};
  fn();
  auto const cpu{thread_cpu_time() - cpu_start};
  auto const wall{std::chrono::steady_clock::now() - wall_start};
  return wall >= std::chrono::milliseconds{50} && cpu * 4 < wall;
}

TEST_CASE("nexenne::logging async manager flush parks while a slow sink drains") {
  using slow_cfg = lg::config<8, true>;
  auto& mgr{lg::basic_manager<slow_cfg>::instance()};
  mgr.clear_sinks();
  auto slow{std::make_shared<slow_sink>()};
  mgr.add_sink(slow);

  for (std::size_t i{0}; i < 6; ++i) {
    nexenne::utility::ignore(mgr.push(rec("slow")));
  }
  CHECK(waits_without_spinning([&mgr] { mgr.flush(); }));
  CHECK(slow->count() == 6);

  mgr.clear_sinks();
}

TEST_CASE("nexenne::logging async manager push_blocking parks on a full queue") {
  using tiny_cfg = lg::config<2, true>;
  auto& mgr{lg::basic_manager<tiny_cfg>::instance()};
  mgr.clear_sinks();
  auto slow{std::make_shared<slow_sink>()};
  mgr.add_sink(slow);

  CHECK(waits_without_spinning([&mgr] {
    for (std::size_t i{0}; i < 8; ++i) {
      mgr.push_blocking(rec("blocked"));
    }
  }));
  mgr.flush();
  CHECK(slow->count() == 8);
  CHECK(mgr.dropped_count() == 0);

  mgr.clear_sinks();
}

#endif

}  // namespace
