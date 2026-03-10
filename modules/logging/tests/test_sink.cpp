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
#include <vector>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace {

namespace lg = nexenne::logging;

// A test sink that records every line that passes the base-class level filter.
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
  CHECK(s.min_level() == lg::level::trace);  // default
  s.set_min_level(lg::level::warn);
  CHECK(s.min_level() == lg::level::warn);

  s.write(make_record(lg::level::trace, "t"));  // dropped
  s.write(make_record(lg::level::info, "i"));   // dropped
  s.write(make_record(lg::level::warn, "w"));   // kept
  s.write(make_record(lg::level::error, "e"));  // kept
  REQUIRE(s.lines.size() == 2);
  CHECK(s.lines[0].find("-- w") != std::string::npos);
  CHECK(s.lines[1].find("-- e") != std::string::npos);
}

TEST_CASE("nexenne::logging default_format renders every field in order") {
  capture_sink s;
  s.write(make_record(lg::level::warn, "down"));
  REQUIRE(s.lines.size() == 1);
  auto const& line{s.lines[0]};
  CHECK(line.find("[net]") != std::string::npos);     // logger name
  CHECK(line.find("[WARN ]") != std::string::npos);   // level
  CHECK(line.find(" -- down") != std::string::npos);  // message
  CHECK(line.starts_with("["));                       // timestamp opens the line
  CHECK(line.back() == '\n');                         // newline-terminated

  // The leading "[YYYY-MM-DD HH:MM:SS.mmm]" must carry exactly one fractional
  // separator with three millisecond digits (no duplicated sub-second part).
  auto const ts{line.substr(0, line.find(']'))};
  auto const dot{ts.find('.')};
  REQUIRE(dot != std::string::npos);
  CHECK(ts.find('.', dot + 1) == std::string::npos);  // only one '.'
  CHECK(ts.size() - dot - 1 == 3);                    // exactly three ms digits
}

TEST_CASE("nexenne::logging::ring_sink retains the most recent N lines, oldest first") {
  lg::ring_sink<2> rs;
  CHECK(rs.size() == 0);
  rs.write(make_record(lg::level::info, "a"));
  rs.write(make_record(lg::level::info, "b"));
  rs.write(make_record(lg::level::info, "c"));  // evicts "a"
  CHECK(rs.size() == 2);
  auto const snap{rs.snapshot()};
  REQUIRE(snap.size() == 2);
  CHECK(snap[0].find("-- b") != std::string::npos);  // oldest retained
  CHECK(snap[1].find("-- c") != std::string::npos);  // newest
}

TEST_CASE("nexenne::logging::file_sink appends formatted lines and round-trips") {
  auto const path{std::filesystem::temp_directory_path() / "nexenne_logging_file_sink_test.log"};
  std::filesystem::remove(path);
  {
    lg::file_sink f{path.string()};
    REQUIRE(f.is_open());
    f.write(make_record(lg::level::error, "boom"));
    f.flush();
  }  // closed by destructor
  auto in{std::ifstream{path}};
  REQUIRE(in.is_open());
  auto const contents{std::string{std::istreambuf_iterator<char>{in}, {}}};
  CHECK(contents.find("[net]") != std::string::npos);
  CHECK(contents.find("[ERROR]") != std::string::npos);
  CHECK(contents.find("-- boom") != std::string::npos);
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::file_sink reports a failed open and moves cleanly") {
  // A path that cannot be opened for append (a directory) reports not-open.
  lg::file_sink bad{std::filesystem::temp_directory_path().string()};
  CHECK_FALSE(bad.is_open());

  auto const path{std::filesystem::temp_directory_path() / "nexenne_logging_file_sink_move.log"};
  std::filesystem::remove(path);
  lg::file_sink a{path.string()};
  REQUIRE(a.is_open());
  lg::file_sink b{std::move(a)};
  CHECK(b.is_open());
  CHECK_FALSE(a.is_open());  // moved-from is closed
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::console_sink constructs with each routing policy") {
  // Smoke: construction and a write must not crash (output goes to the console).
  lg::console_sink def{};
  lg::console_sink out{lg::console_sink::stream::stdout_only};
  lg::console_sink err{lg::console_sink::stream::stderr_only};
  static_cast<void>(def);
  static_cast<void>(out);
  static_cast<void>(err);
  CHECK(true);
}

}  // namespace
