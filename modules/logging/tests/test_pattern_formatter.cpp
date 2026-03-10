/**
 * @file
 * @brief Tests for nexenne::logging::pattern_formatter token expansion.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/pattern_formatter.hpp>
#include <nexenne/logging/record.hpp>

namespace {

namespace lg = nexenne::logging;

// Builds a record with a fixed, known wall-clock instant so timestamp tokens
// render deterministically: 2026-01-02T03:04:05.678 UTC.
[[nodiscard]] auto
make_record(lg::level const sev, std::string_view const name, std::string msg) -> lg::record {
  auto r{lg::record{sev, std::source_location::current(), name, std::move(msg)}};
  auto const epoch{std::chrono::sys_days{std::chrono::January / 2 / 2026}};
  r.timestamp = std::chrono::system_clock::time_point{epoch} + std::chrono::hours{3}
                + std::chrono::minutes{4} + std::chrono::seconds{5}
                + std::chrono::milliseconds{678};
  return r;
}

TEST_CASE("nexenne::logging::pattern_formatter default constructs with default_pattern") {
  auto const f{lg::pattern_formatter{}};
  CHECK(f.pattern() == lg::pattern_formatter::default_pattern);
  CHECK(f.pattern() == "[%T] [%L] [%n] %m (%f:%#)");
}

TEST_CASE("nexenne::logging::pattern_formatter default pattern renders a full line") {
  auto const f{lg::pattern_formatter{}};
  auto const r{make_record(lg::level::warn, "net", "link down")};
  auto const out{f.format(r)};
  // [%T] [%L] [%n] %m (%f:%#)
  CHECK(out.starts_with("[03:04:05.678] [W] [net] link down ("));
  CHECK(out.find("test_pattern_formatter.cpp:") != std::string::npos);
  CHECK(out.back() == ')');
}

TEST_CASE("nexenne::logging::pattern_formatter %t renders ISO 8601 UTC with date") {
  auto const f{lg::pattern_formatter{std::string{"%t"}}};
  auto const r{make_record(lg::level::info, "x", "m")};
  CHECK(f.format(r) == "2026-01-02T03:04:05.678Z");
}

TEST_CASE("nexenne::logging::pattern_formatter %T renders time only") {
  auto const f{lg::pattern_formatter{std::string{"%T"}}};
  auto const r{make_record(lg::level::info, "x", "m")};
  CHECK(f.format(r) == "03:04:05.678");
}

TEST_CASE("nexenne::logging::pattern_formatter %l renders the full unpadded level name") {
  auto const r_trace{make_record(lg::level::trace, "x", "m")};
  auto const r_crit{make_record(lg::level::critical, "x", "m")};
  auto const r_off{make_record(lg::level::off, "x", "m")};
  auto const f{lg::pattern_formatter{std::string{"%l"}}};
  CHECK(f.format(r_trace) == "TRACE");
  CHECK(f.format(make_record(lg::level::debug, "x", "m")) == "DEBUG");
  CHECK(f.format(make_record(lg::level::info, "x", "m")) == "INFO");
  CHECK(f.format(make_record(lg::level::warn, "x", "m")) == "WARN");
  CHECK(f.format(make_record(lg::level::error, "x", "m")) == "ERROR");
  CHECK(f.format(r_crit) == "CRITICAL");
  CHECK(f.format(r_off) == "OFF");
}

TEST_CASE("nexenne::logging::pattern_formatter %L renders the single-char level tag") {
  auto const f{lg::pattern_formatter{std::string{"%L"}}};
  CHECK(f.format(make_record(lg::level::trace, "x", "m")) == "T");
  CHECK(f.format(make_record(lg::level::debug, "x", "m")) == "D");
  CHECK(f.format(make_record(lg::level::info, "x", "m")) == "I");
  CHECK(f.format(make_record(lg::level::warn, "x", "m")) == "W");
  CHECK(f.format(make_record(lg::level::error, "x", "m")) == "E");
  CHECK(f.format(make_record(lg::level::critical, "x", "m")) == "C");
  CHECK(f.format(make_record(lg::level::off, "x", "m")) == "-");
}

TEST_CASE("nexenne::logging::pattern_formatter %n renders the logger name") {
  auto const f{lg::pattern_formatter{std::string{"%n"}}};
  CHECK(f.format(make_record(lg::level::info, "subsystem", "m")) == "subsystem");
  CHECK(f.format(make_record(lg::level::info, "", "m")).empty());  // empty name
}

TEST_CASE("nexenne::logging::pattern_formatter %m renders the message body") {
  auto const f{lg::pattern_formatter{std::string{"%m"}}};
  CHECK(f.format(make_record(lg::level::info, "x", "hello world")) == "hello world");
  CHECK(f.format(make_record(lg::level::info, "x", "")).empty());  // empty message
}

TEST_CASE("nexenne::logging::pattern_formatter %f renders the file basename only") {
  auto const f{lg::pattern_formatter{std::string{"%f"}}};
  // source_location::current() yields this file with whatever path the build
  // used; the formatter must strip every directory component.
  auto const out{f.format(make_record(lg::level::info, "x", "m"))};
  CHECK(out == "test_pattern_formatter.cpp");
  CHECK(out.find('/') == std::string::npos);
  CHECK(out.find('\\') == std::string::npos);
}

TEST_CASE("nexenne::logging::pattern_formatter %# renders the source line number") {
  auto const f{lg::pattern_formatter{std::string{"%#"}}};
  auto r{lg::record{lg::level::info, std::source_location::current(), "x", std::string{"m"}}};
  auto const expected{std::to_string(r.location.line())};
  CHECK(f.format(r) == expected);
}

TEST_CASE("nexenne::logging::pattern_formatter %s renders the function name") {
  auto const f{lg::pattern_formatter{std::string{"%s"}}};
  auto const r{make_record(lg::level::info, "x", "m")};
  auto const out{f.format(r)};
  // The function name string is compiler-specific; just require it is non-empty
  // and matches what the location reports.
  CHECK(out == std::string_view{r.location.function_name()});
  CHECK_FALSE(out.empty());
}

TEST_CASE("nexenne::logging::pattern_formatter %% renders a literal percent") {
  auto const f{lg::pattern_formatter{std::string{"100%% done"}}};
  CHECK(f.format(make_record(lg::level::info, "x", "m")) == "100% done");
}

TEST_CASE("nexenne::logging::pattern_formatter emits literal text verbatim") {
  auto const f{lg::pattern_formatter{std::string{"plain text, no tokens"}}};
  CHECK(f.format(make_record(lg::level::info, "x", "m")) == "plain text, no tokens");
}

TEST_CASE("nexenne::logging::pattern_formatter an unknown token keeps the percent and char") {
  auto const f{lg::pattern_formatter{std::string{"a%zb"}}};
  CHECK(f.format(make_record(lg::level::info, "x", "m")) == "a%zb");
}

TEST_CASE("nexenne::logging::pattern_formatter a trailing percent is emitted verbatim") {
  // A '%' at the very end has no following character and is kept literally.
  auto const f{lg::pattern_formatter{std::string{"done%"}}};
  CHECK(f.format(make_record(lg::level::info, "x", "m")) == "done%");
}

TEST_CASE("nexenne::logging::pattern_formatter an empty pattern produces empty output") {
  auto const f{lg::pattern_formatter{std::string{}}};
  CHECK(f.format(make_record(lg::level::info, "x", "msg")).empty());
}

TEST_CASE("nexenne::logging::pattern_formatter format appends without clearing out") {
  auto const f{lg::pattern_formatter{std::string{"%m"}}};
  auto out{std::string{"prefix: "}};
  f.format(make_record(lg::level::info, "x", "body"), out);
  CHECK(out == "prefix: body");
}

TEST_CASE("nexenne::logging::pattern_formatter set_pattern swaps the active pattern") {
  auto f{lg::pattern_formatter{}};
  f.set_pattern(std::string{"%n/%m"});
  CHECK(f.pattern() == "%n/%m");
  CHECK(f.format(make_record(lg::level::info, "io", "boom")) == "io/boom");
}

TEST_CASE("nexenne::logging::pattern_formatter adjacent and repeated tokens expand") {
  auto const f{lg::pattern_formatter{std::string{"%L%L%n%n"}}};
  CHECK(f.format(make_record(lg::level::error, "ab", "m")) == "EEabab");
}

TEST_CASE("nexenne::logging::pattern_formatter combines every token in one pattern") {
  auto const f{lg::pattern_formatter{std::string{"%t|%T|%l|%L|%n|%m|%f|%#|%%"}}};
  auto r{make_record(lg::level::warn, "net", "down")};
  auto const out{f.format(r)};
  CHECK(out.find("2026-01-02T03:04:05.678Z|") != std::string::npos);
  CHECK(out.find("|03:04:05.678|") != std::string::npos);
  CHECK(out.find("|WARN|W|net|down|") != std::string::npos);
  CHECK(out.find("|test_pattern_formatter.cpp|") != std::string::npos);
  CHECK(out.ends_with("|%"));
}

TEST_CASE("nexenne::logging::pattern_formatter tolerates a null source-location file name") {
  // A default-constructed source_location may report a null file/function name
  // on some implementations; the formatter must render "?" rather than form a
  // string_view from a null pointer (undefined behaviour).
  auto r{lg::record{lg::level::info, std::source_location::current(), "x", std::string{"m"}}};
  r.location = std::source_location{};  // unspecified, possibly null, name fields
  auto const f{lg::pattern_formatter{std::string{"%f|%s"}}};
  auto const out{f.format(r)};
  // Either the implementation supplies a real name, or the formatter substitutes
  // "?"; in neither case may it crash, and the separator must survive.
  CHECK(out.find('|') != std::string::npos);
}

}  // namespace
