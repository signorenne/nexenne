/**
 * @file
 * @brief End-to-end tests for basic_logger, format_string, and the LOG_* macros.
 *
 * A synchronous config is used so a log call dispatches inline and the captured
 * records can be inspected without flushing or sleeping.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/logging/config.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/logger.hpp>
#include <nexenne/logging/macros.hpp>
#include <nexenne/logging/manager.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace {

namespace lg = nexenne::logging;

class capture_sink final : public lg::sink {
public:
  [[nodiscard]] auto records() const -> std::vector<lg::record> {
    auto const guard{std::lock_guard{m_mutex}};
    return m_records;
  }

  [[nodiscard]] auto count() const noexcept -> std::size_t {
    return m_count.load(std::memory_order_acquire);
  }

protected:
  auto write_out(lg::record const& r) noexcept -> void override {
    {
      auto const guard{std::lock_guard{m_mutex}};
      m_records.push_back(r);
    }
    m_count.fetch_add(1, std::memory_order_release);
  }

  auto flush_out() noexcept -> void override {}

private:
  mutable std::mutex m_mutex;
  std::vector<lg::record> m_records;
  std::atomic<std::size_t> m_count{0};
};

using sync_cfg = lg::config<1, false>;

// Registers a fresh capture sink on the sync manager and returns it; clears any
// previously registered sinks so each test starts clean.
[[nodiscard]] auto fresh_sink() -> std::shared_ptr<capture_sink> {
  auto& mgr{lg::basic_manager<sync_cfg>::instance()};
  mgr.clear_sinks();
  auto cap{std::make_shared<capture_sink>()};
  mgr.add_sink(cap);
  return cap;
}

TEST_CASE("nexenne::logging::basic_logger routes each level method to its severity") {
  auto const cap{fresh_sink()};
  lg::basic_logger<sync_cfg> log{"app"};

  log.trace("t");
  log.debug("d");
  log.info("i");
  log.warn("w");
  log.error("e");
  log.critical("c");

  auto const got{cap->records()};
  REQUIRE(got.size() == 6);
  CHECK(got[0].severity == lg::level::trace);
  CHECK(got[1].severity == lg::level::debug);
  CHECK(got[2].severity == lg::level::info);
  CHECK(got[3].severity == lg::level::warn);
  CHECK(got[4].severity == lg::level::error);
  CHECK(got[5].severity == lg::level::critical);
  CHECK(got[2].logger_name == "app");
  CHECK(std::string_view{log.name()} == "app");
}

TEST_CASE("nexenne::logging::basic_logger drops calls below the runtime minimum level") {
  auto const cap{fresh_sink()};
  lg::basic_logger<sync_cfg> log{"app"};
  log.set_min_level(lg::level::warn);

  CHECK(log.min_level() == lg::level::warn);
  CHECK_FALSE(log.enabled(lg::level::info));
  CHECK(log.enabled(lg::level::warn));
  CHECK(log.enabled(lg::level::error));

  log.trace("dropped");
  log.info("dropped");
  log.warn("kept");
  log.error("kept");

  auto const got{cap->records()};
  REQUIRE(got.size() == 2);
  CHECK(got[0].message == "kept");
  CHECK(got[0].severity == lg::level::warn);
  CHECK(got[1].severity == lg::level::error);
}

TEST_CASE("nexenne::logging::basic_logger formats arguments and captures the call site") {
  auto const cap{fresh_sink()};
  lg::basic_logger<sync_cfg> log{"app"};

  log.info("x={} y={}", 1, 2);

  auto const got{cap->records()};
  REQUIRE(got.size() == 1);
  CHECK(got[0].message == "x=1 y=2");
  // The format_string wrapper captures this file and a positive line number.
  CHECK(
    std::string_view{got[0].location.file_name()}.find("test_logger.cpp") != std::string_view::npos
  );
  CHECK(got[0].location.line() > 0);
}

TEST_CASE("nexenne::logging LOG_*_TO macros emit through a supplied logger") {
  auto const cap{fresh_sink()};
  lg::basic_logger<sync_cfg> log{"app"};

  LOG_INFO_TO(log, "hi {}", 5);
  LOG_ERROR_TO(log, "boom");

  auto const got{cap->records()};
  REQUIRE(got.size() == 2);
  CHECK(got[0].message == "hi 5");
  CHECK(got[0].severity == lg::level::info);
  CHECK(got[1].severity == lg::level::error);
}

TEST_CASE("nexenne::logging LOG_* macros emit through the default async manager") {
  auto& mgr{lg::manager::instance()};
  mgr.clear_sinks();
  auto const cap{std::make_shared<capture_sink>()};
  mgr.add_sink(cap);
  lg::default_logger().set_min_level(lg::level::trace);

  LOG_INFO("hello {}", 42);
  LOG_WARN("careful");
  mgr.flush();

  auto const got{cap->records()};
  REQUIRE(got.size() == 2);
  CHECK(got[0].message == "hello 42");
  CHECK(got[1].message == "careful");
  CHECK(got[1].severity == lg::level::warn);

  mgr.clear_sinks();
}

}  // namespace
