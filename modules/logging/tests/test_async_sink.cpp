/**
 * @file
 * @brief Tests for the async decorator sink (background-thread forwarding).
 */

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
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

// async_sink owns and destroys its inner sink, so captures live in this outliving state.
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

TEST_CASE("nexenne::logging::async_sink forwards every record to the wrapped sink in order") {
  capture_state state;
  constexpr std::size_t total{200};
  {
    lg::async_sink async{std::make_unique<capture_sink>(state)};
    for (std::size_t i{0}; i < total; ++i) {
      async.write(make_record(std::to_string(i)));
    }
  }
  CHECK(state.count.load() == total);
  auto const got{state.snapshot()};
  REQUIRE(got.size() == total);
  for (std::size_t i{0}; i < total; ++i) {
    CHECK(got[i] == std::to_string(i));
  }
}

TEST_CASE("nexenne::logging::async_sink reports the configuration it was built with") {
  capture_state state;
  lg::async_sink const async{
    std::make_unique<capture_sink>(state),
    lg::async_sink::config{.queue_size_limit = 8, .on_overflow = lg::overflow_action::drop_oldest}
  };
  CHECK(async.configuration().queue_size_limit == 8);
  CHECK(async.configuration().on_overflow == lg::overflow_action::drop_oldest);
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
    for (std::size_t i{0}; i < 1000; ++i) {
      async.write(make_record(std::to_string(i)));
    }
    state.release.store(true, std::memory_order_release);
  }
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
  }
  CHECK(state.count.load() == total);
}

TEST_CASE("nexenne::logging::async_sink handles the queue_size_limit == 1 boundary") {
  SUBCASE("block delivers every record through a single-slot queue") {
    capture_state state;
    lg::async_sink::config cfg{};
    cfg.queue_size_limit = 1;
    cfg.on_overflow = lg::overflow_action::block;
    constexpr std::size_t total{50};
    {
      lg::async_sink async{std::make_unique<capture_sink>(state), cfg};
      for (std::size_t i{0}; i < total; ++i) {
        async.write(make_record(std::to_string(i)));
      }
    }
    CHECK(state.count.load() == total);
  }

  SUBCASE("drop_oldest never pops an empty single-slot queue") {
    slow_state state;
    lg::async_sink::config cfg{};
    cfg.queue_size_limit = 1;
    cfg.on_overflow = lg::overflow_action::drop_oldest;
    {
      lg::async_sink async{std::make_unique<slow_sink>(state), cfg};
      for (std::size_t i{0}; i < 100; ++i) {
        async.write(make_record(std::to_string(i)));
      }
      state.release.store(true, std::memory_order_release);
    }
    CHECK(state.seen.load() >= 1);
  }
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
  CHECK(state.count.load() == producers * per_producer);
}

struct overlap_state {
  std::atomic<int> inside{0};
  std::atomic<bool> overlapped{false};
};

class overlap_sink final : public lg::sink {
public:
  explicit overlap_sink(overlap_state& st) noexcept : m_st{st} {}

protected:
  auto write_out(lg::record const&) noexcept -> void override {
    busy(std::chrono::microseconds{20});
  }

  auto flush_out() noexcept -> void override {
    busy(std::chrono::microseconds{500});
  }

private:
  auto busy(std::chrono::microseconds const hold) noexcept -> void {
    if (m_st.inside.fetch_add(1, std::memory_order_acq_rel) != 0) {
      m_st.overlapped.store(true, std::memory_order_relaxed);
    }
    std::this_thread::sleep_for(hold);
    m_st.inside.fetch_sub(1, std::memory_order_acq_rel);
  }

  overlap_state& m_st;
};

TEST_CASE("nexenne::logging::async_sink never flushes the inner sink during a write") {
  auto st{overlap_state{}};
  {
    lg::async_sink s{std::make_unique<overlap_sink>(st)};
    auto producer{std::thread{[&s] {
      for (auto i{0}; i < 2000; ++i) {
        s.write(make_record("x"));
        std::this_thread::sleep_for(std::chrono::microseconds{10});
      }
    }}};
    for (auto i{0}; i < 100; ++i) {
      s.flush();
    }
    producer.join();
  }
  CHECK_FALSE(st.overlapped.load());
}

}  // namespace
