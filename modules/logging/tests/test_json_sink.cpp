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
#include <string_view>
#include <utility>

#include <nexenne/logging/json_sink.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>

namespace {

namespace lg = nexenne::logging;

[[nodiscard]] auto make_record(lg::level const sev, std::string msg) -> lg::record {
  return lg::record{sev, std::source_location::current(), "net", std::move(msg)};
}

[[nodiscard]] auto emit_line(lg::record const& r) -> std::string {
  auto const path{
    std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / "nexenne_logging_json_sink_test.log"
  };
  std::filesystem::remove(path);
  {
    lg::json_sink s{path.string()};
    REQUIRE(s.is_open());
    s.write(r);
    s.flush();
  }
  auto in{std::ifstream{path}};
  REQUIRE(in.is_open());
  auto line{std::string{}};
  std::getline(in, line);
  std::filesystem::remove(path);
  return line;
}

TEST_CASE("nexenne::logging::json_sink emits every field in a single JSON line") {
  auto const line{emit_line(make_record(lg::level::warn, "down"))};

  CHECK(line.starts_with("{\"ts\":\""));
  CHECK(line.ends_with("}"));

  CHECK(line.find("\"level\":\"WARN\"") != std::string::npos);
  CHECK(line.find("\"logger\":\"net\"") != std::string::npos);
  CHECK(line.find("\"file\":\"") != std::string::npos);
  CHECK(line.find("\"msg\":\"down\"") != std::string::npos);

  CHECK(line.find("\"line\":") != std::string::npos);
  CHECK(line.find("\"line\":0") == std::string::npos);
}

TEST_CASE("nexenne::logging::json_sink trims the level-name padding") {
  CHECK(
    emit_line(make_record(lg::level::info, "x")).find("\"level\":\"INFO\"") != std::string::npos
  );
  CHECK(
    emit_line(make_record(lg::level::error, "x")).find("\"level\":\"ERROR\"") != std::string::npos
  );
}

TEST_CASE("nexenne::logging::json_sink emits the canonical CRITICAL token and a tid field") {
  auto const line{emit_line(make_record(lg::level::critical, "boom"))};
  CHECK(line.find("\"level\":\"CRITICAL\"") != std::string::npos);
  CHECK(line.find("\"tid\":\"") != std::string::npos);
}

TEST_CASE("nexenne::logging::json_sink escapes JSON-significant characters in the message") {
  auto const msg{std::string{"a\"b\\c\nd\te"} + std::string(1, '\x01') + "f"};
  auto const line{emit_line(make_record(lg::level::info, msg))};

  CHECK(line.find("\\\"") != std::string::npos);
  CHECK(line.find("\\\\") != std::string::npos);
  CHECK(line.find("\\n") != std::string::npos);
  CHECK(line.find("\\t") != std::string::npos);
  CHECK(line.find("\\u0001") != std::string::npos);

  CHECK(line.find('\n') == std::string::npos);
  CHECK(line.find('\t') == std::string::npos);
  CHECK(line.find('\x01') == std::string::npos);

  CHECK(line.find("a\"b\\c") == std::string::npos);

  auto const key{std::string{"\"msg\":\""}};
  auto const pos{line.find(key)};
  REQUIRE(pos != std::string::npos);
  auto const value{line.substr(pos + key.size())};
  CHECK(value == "a\\\"b\\\\c\\nd\\te\\u0001f\"}");
}

TEST_CASE("nexenne::logging::json_sink escapes carriage return, backspace and form feed") {
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
  auto const ts{line.substr(pos + key.size(), 24)};

  CHECK(ts.size() == 24);
  CHECK(ts[4] == '-');
  CHECK(ts[7] == '-');
  CHECK(ts[10] == 'T');
  CHECK(ts[13] == ':');
  CHECK(ts[16] == ':');
  CHECK(ts[19] == '.');
  CHECK(ts[23] == 'Z');
  CHECK(ts.find('.', 20) == std::string::npos);
}

TEST_CASE("nexenne::logging::json_sink to an external FILE* does not close it") {
  auto const path{
    std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / "nexenne_logging_json_sink_ext.log"
  };
  std::filesystem::remove(path);
  auto* const out{std::fopen(path.string().c_str(), "wb")};
  REQUIRE(out != nullptr);
  {
    lg::json_sink s{out};
    CHECK(s.is_open());
    s.write(make_record(lg::level::info, "external"));
    s.flush();
  }
  CHECK(std::fflush(out) == 0);
  CHECK(std::fclose(out) == 0);
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::json_sink reports a failed open of a directory path") {
  lg::json_sink bad{std::string_view{NEXENNE_LOGGING_TEST_DIR}};
  CHECK_FALSE(bad.is_open());
}

}  // namespace
