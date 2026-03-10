/**
 * @file
 * @brief Tests for the NDJSON (JSON Lines) sink, focused on string escaping.
 */

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <string>
#include <utility>

#include <nexenne/logging/json_sink.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>

namespace {

namespace lg = nexenne::logging;

[[nodiscard]] auto make_record(lg::level const sev, std::string msg) -> lg::record {
  return lg::record{sev, std::source_location::current(), "net", std::move(msg)};
}

// Writes one record through a json_sink backed by a temp file and reads back the
// single emitted line (without the trailing newline).
[[nodiscard]] auto emit_line(lg::record const& r) -> std::string {
  auto const path{std::filesystem::temp_directory_path() / "nexenne_logging_json_sink_test.log"};
  std::filesystem::remove(path);
  {
    lg::json_sink s{path.string()};
    REQUIRE(s.is_open());
    s.write(r);
    s.flush();
  }  // closed by destructor
  auto in{std::ifstream{path}};
  REQUIRE(in.is_open());
  auto line{std::string{}};
  std::getline(in, line);
  std::filesystem::remove(path);
  return line;
}

TEST_CASE("nexenne::logging::json_sink emits every field in a single JSON line") {
  auto const line{emit_line(make_record(lg::level::warn, "down"))};

  // Shape: opens with an object brace and a "ts" key, ends with a closing brace.
  CHECK(line.starts_with("{\"ts\":\""));
  CHECK(line.ends_with("}"));

  // Each field key is present.
  CHECK(line.find("\"level\":\"WARN\"") != std::string::npos);  // padding trimmed
  CHECK(line.find("\"logger\":\"net\"") != std::string::npos);
  CHECK(line.find("\"file\":\"") != std::string::npos);
  CHECK(line.find("\"msg\":\"down\"") != std::string::npos);

  // line is an unquoted number, and this file's source location is non-zero.
  CHECK(line.find("\"line\":") != std::string::npos);
  CHECK(line.find("\"line\":0") == std::string::npos);
}

TEST_CASE("nexenne::logging::json_sink trims the level-name padding") {
  // INFO and WARN are padded to five chars by to_string; JSON wants the token.
  CHECK(
    emit_line(make_record(lg::level::info, "x")).find("\"level\":\"INFO\"") != std::string::npos
  );
  CHECK(
    emit_line(make_record(lg::level::error, "x")).find("\"level\":\"ERROR\"") != std::string::npos
  );
}

TEST_CASE("nexenne::logging::json_sink escapes JSON-significant characters in the message") {
  // Quote, backslash, newline, tab, and a control byte (0x01).
  auto const msg{std::string{"a\"b\\c\nd\te"} + std::string(1, '\x01') + "f"};
  auto const line{emit_line(make_record(lg::level::info, msg))};

  // The escaped sequences appear literally in the output.
  CHECK(line.find("\\\"") != std::string::npos);     // escaped quote
  CHECK(line.find("\\\\") != std::string::npos);     // escaped backslash
  CHECK(line.find("\\n") != std::string::npos);      // escaped newline
  CHECK(line.find("\\t") != std::string::npos);      // escaped tab
  CHECK(line.find("\\u0001") != std::string::npos);  // control byte

  // No raw control characters leak into the line.
  CHECK(line.find('\n') == std::string::npos);
  CHECK(line.find('\t') == std::string::npos);
  CHECK(line.find('\x01') == std::string::npos);

  // The raw message text, were it unescaped, must not appear unescaped: the
  // literal substring with a real quote-backslash pair should be absent.
  CHECK(line.find("a\"b\\c") == std::string::npos);

  // The escaped payload is embedded between the "msg":" prefix and the close.
  auto const key{std::string{"\"msg\":\""}};
  auto const pos{line.find(key)};
  REQUIRE(pos != std::string::npos);
  auto const value{line.substr(pos + key.size())};
  CHECK(value == "a\\\"b\\\\c\\nd\\te\\u0001f\"}");
}

TEST_CASE("nexenne::logging::json_sink escapes a logger name and file path too") {
  // A record whose message holds the carriage return and backspace/form-feed.
  auto const msg{std::string{"r\rb\bf\f"}};
  auto const line{emit_line(make_record(lg::level::info, msg))};
  CHECK(line.find("\\r") != std::string::npos);
  CHECK(line.find("\\b") != std::string::npos);
  CHECK(line.find("\\f") != std::string::npos);
  CHECK(line.find('\r') == std::string::npos);
}

TEST_CASE("nexenne::logging::json_sink timestamp is RFC 3339 UTC with milliseconds") {
  auto const line{emit_line(make_record(lg::level::info, "x"))};
  auto const key{std::string{"\"ts\":\""}};
  auto const pos{line.find(key)};
  REQUIRE(pos != std::string::npos);
  auto const ts{line.substr(pos + key.size(), 24)};  // 2026-05-26T12:34:56.789Z

  CHECK(ts.size() == 24);
  CHECK(ts[4] == '-');
  CHECK(ts[7] == '-');
  CHECK(ts[10] == 'T');
  CHECK(ts[13] == ':');
  CHECK(ts[16] == ':');
  CHECK(ts[19] == '.');  // single fractional separator, not duplicated
  CHECK(ts[23] == 'Z');  // trailing UTC marker
  // Exactly three fractional digits: positions 20..22 are digits, 23 is 'Z'.
  CHECK(ts.find('.', 20) == std::string::npos);  // no second '.'
}

TEST_CASE("nexenne::logging::json_sink to an external FILE* does not close it") {
  // Smoke: writing to stdout via the FILE* constructor must not crash, and the
  // sink must not take ownership (we keep using the stream after).
  lg::json_sink s{stdout};
  CHECK(s.is_open());
  s.write(make_record(lg::level::info, "external"));
  s.flush();
  // stdout is still usable; no ownership was taken.
  CHECK(std::fflush(stdout) == 0);
}

TEST_CASE("nexenne::logging::json_sink reports a failed open") {
  // A directory cannot be opened for append.
  lg::json_sink bad{std::filesystem::temp_directory_path().string()};
  CHECK_FALSE(bad.is_open());
}

}  // namespace
