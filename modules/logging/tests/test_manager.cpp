/**
 * @file
 * @brief Tests for the sync and async manager backends and the selector.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
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
#include <nexenne/utility/discard.hpp>

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

// Distinct configs give distinct manager singletons, isolating these tests.
using sync_cfg = lg::config<1, false>;
using async_cfg = lg::config<1024, true>;

TEST_CASE("nexenne::logging sync manager dispatches inline to every registered sink") {
  auto& mgr{lg::basic_manager<sync_cfg>::instance()};
  mgr.clear_sinks();
  CHECK(mgr.sink_count() == 0);

  auto cap{std::make_shared<capture_sink>()};
  mgr.add_sink(cap);
  CHECK(mgr.sink_count() == 1);

  nexenne::utility::discard(mgr.push(rec("a")));
  nexenne::utility::discard(mgr.push(rec("b")));
  CHECK(cap->count() == 2);  // synchronous: no flush needed
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
    nexenne::utility::discard(mgr.push(rec(std::to_string(i))));
  }
  mgr.flush();
  CHECK(cap->count() == total);
  CHECK(mgr.dropped_count() == 0);

  mgr.clear_sinks();
}

TEST_CASE("nexenne::logging async manager loses no record under concurrent producers") {
  // Stresses the atomic wait/notify wakeup: many producers bump the signal while
  // the single backend drains. A lost wakeup would stall flush() (the queue never
  // empties); a lost record would make the count fall short. push_blocking never
  // drops, so every record must arrive.
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
  CHECK(mgr.dropped_count() == 0);  // push_blocking never drops

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

}  // namespace
