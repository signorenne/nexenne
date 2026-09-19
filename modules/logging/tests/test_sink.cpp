/**
 * @file
 * @brief Tests for the sink interface, level filter, and bundled sinks.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace lg = nexenne::logging;

class capture_sink final : public lg::sink {
public:
  std::vector<std::string> lines;

protected:
  auto write_out(lg::record const& r) noexcept -> void override {
    lines.push_back(default_format(r));
  }

  auto flush_out() noexcept -> void override {}
};

[[nodiscard]] auto make_record(lg::level const sev, std::string msg) -> lg::record {
  return lg::record{sev, std::source_location::current(), "net", std::move(msg)};
}

TEST_CASE("nexenne::logging::sink filters records below its minimum level") {
  capture_sink s;
  CHECK(s.min_level() == lg::level::trace);
  s.set_min_level(lg::level::warn);
  CHECK(s.min_level() == lg::level::warn);

  s.write(make_record(lg::level::trace, "t"));
  s.write(make_record(lg::level::info, "i"));
  s.write(make_record(lg::level::warn, "w"));
  s.write(make_record(lg::level::error, "e"));
  REQUIRE(s.lines.size() == 2);
  CHECK(s.lines[0].find("-- w") != std::string::npos);
  CHECK(s.lines[1].find("-- e") != std::string::npos);
}

TEST_CASE("nexenne::logging default_format renders every field in order") {
  capture_sink s;
  s.write(make_record(lg::level::warn, "down"));
  REQUIRE(s.lines.size() == 1);
  auto const& line{s.lines[0]};
  CHECK(line.find("[net]") != std::string::npos);
  CHECK(line.find("[WARN ]") != std::string::npos);
  CHECK(line.find(" -- down") != std::string::npos);
  CHECK(line.starts_with("["));
  CHECK(line.back() == '\n');

  auto const ts{line.substr(0, line.find(']'))};
  auto const dot{ts.find('.')};
  REQUIRE(dot != std::string::npos);
  CHECK(ts.find('.', dot + 1) == std::string::npos);
  CHECK(ts.size() - dot - 1 == 3);
}

TEST_CASE("nexenne::logging::ring_sink retains the most recent N lines, oldest first") {
  lg::ring_sink<2> rs;
  CHECK(rs.size() == 0);
  rs.write(make_record(lg::level::info, "a"));
  rs.write(make_record(lg::level::info, "b"));
  rs.write(make_record(lg::level::info, "c"));
  CHECK(rs.size() == 2);
  auto const snap{rs.snapshot()};
  REQUIRE(snap.size() == 2);
  CHECK(snap[0].find("-- b") != std::string::npos);
  CHECK(snap[1].find("-- c") != std::string::npos);
}

TEST_CASE("nexenne::logging::file_sink appends formatted lines and round-trips") {
  auto const path{
    std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / "nexenne_logging_file_sink_test.log"
  };
  std::filesystem::remove(path);
  {
    lg::file_sink f{path.string()};
    REQUIRE(f.is_open());
    f.write(make_record(lg::level::error, "boom"));
    f.flush();
  }
  auto in{std::ifstream{path}};
  REQUIRE(in.is_open());
  auto const contents{std::string{std::istreambuf_iterator<char>{in}, {}}};
  CHECK(contents.find("[net]") != std::string::npos);
  CHECK(contents.find("[ERROR]") != std::string::npos);
  CHECK(contents.find("-- boom") != std::string::npos);
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::file_sink reports a failed open of a directory path") {
  lg::file_sink bad{std::string_view{NEXENNE_LOGGING_TEST_DIR}};
  CHECK_FALSE(bad.is_open());
}

TEST_CASE("nexenne::logging::file_sink is not movable and keeps its level filter") {
  static_assert(!std::is_move_constructible_v<lg::file_sink>);
  static_assert(!std::is_move_assignable_v<lg::file_sink>);
  static_assert(!std::is_copy_constructible_v<lg::file_sink>);

  auto const path{
    std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / "nexenne_logging_file_sink_level.log"
  };
  std::filesystem::remove(path);
  lg::file_sink f{path.string()};
  REQUIRE(f.is_open());
  f.set_min_level(lg::level::error);
  CHECK(f.min_level() == lg::level::error);
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::console_sink constructs with each routing policy") {
  [[maybe_unused]] lg::console_sink def{};
  [[maybe_unused]] lg::console_sink out{lg::console_sink::stream::stdout_only};
  [[maybe_unused]] lg::console_sink err{lg::console_sink::stream::stderr_only};
  CHECK(true);
}

TEST_CASE("nexenne::logging::console_sink reports the routing policy it was built with") {
  CHECK(lg::console_sink{}.routing() == lg::console_sink::stream::auto_split);
  CHECK(
    lg::console_sink{lg::console_sink::stream::stderr_only}.routing()
    == lg::console_sink::stream::stderr_only
  );
}

TEST_CASE("nexenne::logging::sink drops a record at level::off") {
  capture_sink s;
  s.set_min_level(lg::level::off);
  s.write(make_record(lg::level::off, "o"));
  CHECK(s.lines.empty());
}

}  // namespace
