/**
 * @file
 * @brief Tests for the async decorator sink (background-thread forwarding).
 */

#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/logging/async_sink.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace {

namespace lg = nexenne::logging;

// The async_sink OWNS (and destroys) the wrapped sink, so the capture target is
// kept in a test-owned state object that outlives the async_sink.
struct capture_state {
  mutable std::mutex mutex;
  std::vector<std::string> messages;
  std::atomic<std::size_t> count{0};

  [[nodiscard]] auto snapshot() const -> std::vector<std::string> {
    auto const guard{std::lock_guard{mutex}};
    return messages;
  }
};

class capture_sink final : public lg::sink {
public:
  explicit capture_sink(capture_state& st) noexcept : m_st{st} {}

protected:
  auto write_out(lg::record const& r) noexcept -> void override {
    {
      auto const guard{std::lock_guard{m_st.mutex}};
      m_st.messages.push_back(r.message);
    }
    m_st.count.fetch_add(1, std::memory_order_release);
  }

  auto flush_out() noexcept -> void override {}

private:
  capture_state& m_st;
};

// A slow inner sink that stalls until released, with test-owned counters.
struct slow_state {
  std::atomic<bool> release{false};
  std::atomic<std::size_t> seen{0};
};

class slow_sink final : public lg::sink {
public:
  explicit slow_sink(slow_state& st) noexcept : m_st{st} {}

protected:
  auto write_out(lg::record const&) noexcept -> void override {
    while (!m_st.release.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    m_st.seen.fetch_add(1, std::memory_order_release);
  }

  auto flush_out() noexcept -> void override {}

private:
  slow_state& m_st;
};

[[nodiscard]] auto make_record(std::string msg) -> lg::record {
  return lg::record{lg::level::info, std::source_location::current(), "net", std::move(msg)};
}

TEST_CASE("nexenne::logging::async_sink forwards every record to the wrapped sink") {
  capture_state state;
  constexpr std::size_t total{200};
  {
    lg::async_sink async{std::make_unique<capture_sink>(state)};
    for (std::size_t i{0}; i < total; ++i) {
      async.write(make_record(std::to_string(i)));
    }
    // The destructor drains the queue and joins the worker.
  }  // async (and the capture_sink) destroyed here; state survives.
  CHECK(state.count.load() == total);
  auto const got{state.snapshot()};
  REQUIRE(got.size() == total);
  for (std::size_t i{0}; i < total; ++i) {
    CHECK(got[i] == std::to_string(i));  // single producer/consumer preserves FIFO
  }
}

TEST_CASE("nexenne::logging::async_sink flush waits for the queue to drain") {
  capture_state state;
  constexpr std::size_t total{50};
  lg::async_sink async{std::make_unique<capture_sink>(state)};
  for (std::size_t i{0}; i < total; ++i) {
    async.write(make_record(std::to_string(i)));
  }
  async.flush();
  CHECK(state.count.load() == total);
}

TEST_CASE("nexenne::logging::async_sink drop_newest keeps the queue bounded under overload") {
  slow_state state;
  lg::async_sink::config cfg{};
  cfg.queue_size_limit = 4;
  cfg.on_overflow = lg::overflow_action::drop_newest;
  {
    lg::async_sink async{std::make_unique<slow_sink>(state), cfg};
    // Push far more than the queue holds while the inner sink is stalled;
    // drop_newest must keep write non-blocking and never deadlock.
    for (std::size_t i{0}; i < 1000; ++i) {
      async.write(make_record(std::to_string(i)));
    }
    state.release.store(true, std::memory_order_release);
  }
  // At most the in-flight record plus a queue's worth survived.
  CHECK(state.seen.load() >= 1);
  CHECK(state.seen.load() <= 6);
}

TEST_CASE("nexenne::logging::async_sink drop_oldest neither blocks nor deadlocks") {
  slow_state state;
  lg::async_sink::config cfg{};
  cfg.queue_size_limit = 4;
  cfg.on_overflow = lg::overflow_action::drop_oldest;
  {
    lg::async_sink async{std::make_unique<slow_sink>(state), cfg};
    for (std::size_t i{0}; i < 100; ++i) {
      async.write(make_record(std::to_string(i)));
    }
    state.release.store(true, std::memory_order_release);
  }
  // Most of the 100 were dropped; reaching here proves no deadlock.
  CHECK(state.seen.load() <= 10);
}

TEST_CASE("nexenne::logging::async_sink block policy delivers without dropping") {
  capture_state state;
  lg::async_sink::config cfg{};
  cfg.queue_size_limit = 8;
  cfg.on_overflow = lg::overflow_action::block;
  constexpr std::size_t total{500};
  {
    lg::async_sink async{std::make_unique<capture_sink>(state), cfg};
    for (std::size_t i{0}; i < total; ++i) {
      async.write(make_record(std::to_string(i)));
    }
  }
  CHECK(state.count.load() == total);
  CHECK(state.snapshot().size() == total);
}

TEST_CASE("nexenne::logging::async_sink shuts down cleanly with pending records") {
  capture_state state;
  constexpr std::size_t total{300};
  {
    lg::async_sink async{std::make_unique<capture_sink>(state)};
    for (std::size_t i{0}; i < total; ++i) {
      async.write(make_record(std::to_string(i)));
    }
    // No flush: the destructor must drain pending records gracefully.
  }
  CHECK(state.count.load() == total);
}

TEST_CASE("nexenne::logging::async_sink accepts records from many producer threads") {
  capture_state state;
  constexpr std::size_t producers{4};
  constexpr std::size_t per_producer{250};
  lg::async_sink::config cfg{};
  cfg.queue_size_limit = 32;
  cfg.on_overflow = lg::overflow_action::block;
  {
    lg::async_sink async{std::make_unique<capture_sink>(state), cfg};
    auto threads{std::vector<std::thread>{}};
    for (std::size_t p{0}; p < producers; ++p) {
      threads.emplace_back([&async, p] {
        for (std::size_t i{0}; i < per_producer; ++i) {
          async.write(make_record(std::to_string(p * per_producer + i)));
        }
      });
    }
    for (auto& t : threads) {
      t.join();
    }
  }
  CHECK(state.count.load() == producers * per_producer);  // block drops nothing
}

}  // namespace
