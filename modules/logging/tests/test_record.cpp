/**
 * @file
 * @brief Tests for nexenne::logging::record construction and stamping.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>

namespace {

namespace lg = nexenne::logging;

TEST_CASE("nexenne::logging::record default-constructs empty at info") {
  auto const r{lg::record{}};
  CHECK(r.severity == lg::level::info);
  CHECK(r.logger_name.empty());
  CHECK(r.message.empty());
}

TEST_CASE("nexenne::logging::record stamps fields, time, and producing thread") {
  auto const before{std::chrono::system_clock::now()};
  auto const loc{std::source_location::current()};
  auto const r{lg::record{lg::level::warn, loc, "net", std::string{"link down"}}};
  auto const after{std::chrono::system_clock::now()};

  CHECK(r.severity == lg::level::warn);
  CHECK(r.logger_name == "net");
  CHECK(r.message == "link down");
  CHECK(r.thread_id == std::this_thread::get_id());
  CHECK(r.timestamp >= before);
  CHECK(r.timestamp <= after);
  CHECK(std::string_view{r.location.function_name()} == std::string_view{loc.function_name()});
  CHECK(r.location.line() == loc.line());
}

TEST_CASE("nexenne::logging::record moves the message in rather than copying it") {
  // Longer than any small-string buffer, so a move keeps the heap pointer.
  auto msg{std::string{"a message long enough to force a heap allocation past small-string land"}};
  auto const* const buffer{msg.data()};
  auto const r{lg::record{lg::level::info, std::source_location::current(), "x", std::move(msg)}};
  CHECK(r.message.size() == 71);
  CHECK(r.message.data() == buffer);
}

TEST_CASE("nexenne::logging::record is movable and carries every field") {
  auto a{lg::record{lg::level::error, std::source_location::current(), "io", std::string{"boom"}}};
  auto const tid{a.thread_id};
  auto const stamp{a.timestamp};
  auto const b{std::move(a)};
  CHECK(b.severity == lg::level::error);
  CHECK(b.logger_name == "io");
  CHECK(b.message == "boom");
  CHECK(b.thread_id == tid);
  CHECK(b.timestamp == stamp);
}

TEST_CASE("nexenne::logging::record borrows the logger name") {
  auto const name{std::string_view{"ui"}};
  auto const r{lg::record{lg::level::debug, std::source_location::current(), name, std::string{}}};
  CHECK(r.logger_name.data() == name.data());
  CHECK(r.message.empty());
}

}  // namespace
