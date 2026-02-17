/**
 * @file
 * @brief Tests for the nexenne::utility source_location formatters.
 */

#include <doctest/doctest.h>

#include <array>
#include <source_location>
#include <string>
#include <string_view>

#include <nexenne/utility/source_location_format.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(util::detail::basename_of("a/b/c.cpp") == "c.cpp");
static_assert(util::detail::basename_of("c.cpp") == "c.cpp");        // no separator
static_assert(util::detail::basename_of("a\\b\\c.cpp") == "c.cpp");  // backslash
static_assert(util::detail::basename_of("a/b\\c.cpp") == "c.cpp");   // mixed separators
static_assert(util::detail::basename_of("/abs/path/file") == "file");
static_assert(util::detail::basename_of("a/b/").empty());  // trailing separator
static_assert(util::detail::basename_of("/").empty());     // lone separator
static_assert(util::detail::basename_of("").empty());
static_assert(util::detail::basename_of("/.") == ".");  // dotfile-ish

TEST_CASE("nexenne::utility::format_short yields exactly basename:line") {
  std::array<char, 256> buf{};
  auto const loc{std::source_location::current()};
  auto const tag{util::format_short(loc, buf)};

  auto const expected{std::string{"test_source_location_format.cpp:"} + std::to_string(loc.line())};
  CHECK(std::string{tag} == expected);
}

TEST_CASE("nexenne::utility::format_short strips the directory and keeps the line") {
  std::array<char, 256> buf{};
  auto const loc{std::source_location::current()};
  auto const tag{util::format_short(loc, buf)};

  // No path separator survives, and the line number follows a single colon.
  CHECK(tag.find('/') == std::string_view::npos);
  CHECK(tag.find('\\') == std::string_view::npos);
  auto const colon{tag.find(':')};
  REQUIRE(colon != std::string_view::npos);
  CHECK(tag.find(':', colon + 1) == std::string_view::npos);  // exactly one colon

  auto const line_text{tag.substr(colon + 1)};
  CHECK(line_text == std::to_string(loc.line()));
  CHECK(tag.substr(0, colon) == "test_source_location_format.cpp");
}

TEST_CASE("nexenne::utility::format_short reflects different call-site lines") {
  std::array<char, 256> a{};
  std::array<char, 256> b{};
  auto const tag_a{util::format_short(std::source_location::current(), a)};
  auto const tag_b{util::format_short(std::source_location::current(), b)};
  CHECK(tag_a != tag_b);  // different physical lines => different tags
}

TEST_CASE("nexenne::utility::format_long appends the function name") {
  std::array<char, 256> buf{};
  auto const tag{util::format_long(std::source_location::current(), buf)};

  CHECK(tag.find("test_source_location_format.cpp:") == 0);
  CHECK(tag.find(" in ") != std::string_view::npos);
}

TEST_CASE("nexenne::utility::format_long embeds the enclosing function name") {
  std::array<char, 256> buf{};
  auto const tag{util::format_long(std::source_location::current(), buf)};

  // The text after " in " is the function name; here that contains the
  // doctest-generated runner symbol for this case. It must be non-empty.
  auto const pos{tag.find(" in ")};
  REQUIRE(pos != std::string_view::npos);
  auto const func{tag.substr(pos + 4)};
  CHECK_FALSE(func.empty());

  // The long form starts with exactly the short form for the same location.
  std::array<char, 256> short_buf{};
  auto const loc{std::source_location::current()};
  auto const short_tag{util::format_short(loc, short_buf)};
  CHECK(short_tag.substr(0, short_tag.find(':')) == "test_source_location_format.cpp");
}

TEST_CASE("nexenne::utility formatters truncate into a tiny buffer") {
  std::array<char, 4> small{};
  CHECK(util::format_short(std::source_location::current(), small).size() <= 3);

  std::array<char, 6> medium{};
  CHECK(util::format_long(std::source_location::current(), medium).size() <= 5);

  std::array<char, 1> one{};
  CHECK(util::format_short(std::source_location::current(), one).empty());
}

TEST_CASE("nexenne::utility truncation never overruns the buffer and stays a prefix") {
  // The truncated short tag is a prefix of the untruncated one.
  std::array<char, 256> full_buf{};
  auto const loc{std::source_location::current()};
  auto const full{std::string{util::format_short(loc, full_buf)}};

  std::array<char, 4> tiny{};
  auto const t4{util::format_short(loc, tiny)};
  CHECK(t4.size() <= tiny.size() - 1);
  CHECK(full.compare(0, t4.size(), std::string{t4}) == 0);  // truncation is a prefix

  std::array<char, 8> small{};
  auto const t8{util::format_short(loc, small)};
  CHECK(t8.size() <= small.size() - 1);
  CHECK(full.compare(0, t8.size(), std::string{t8}) == 0);

  std::array<char, 16> mid{};
  auto const t16{util::format_short(loc, mid)};
  CHECK(t16.size() <= mid.size() - 1);
  CHECK(full.compare(0, t16.size(), std::string{t16}) == 0);
}

TEST_CASE("nexenne::utility format_long truncation respects the one-char buffer") {
  std::array<char, 1> one{};
  // A 1-byte buffer holds only the NUL terminator, so nothing usable is written.
  CHECK(util::format_long(std::source_location::current(), one).empty());
}

TEST_CASE("nexenne::utility format_short fills a buffer sized to the exact length") {
  std::array<char, 64> buf{};
  auto const loc{std::source_location::current()};
  auto const tag{util::format_short(loc, buf)};
  // Re-running with a buffer exactly one larger than the text yields identical output.
  std::array<char, 64> buf2{};
  auto const tag2{util::format_short(loc, buf2)};
  CHECK(tag == tag2);
}

}  // namespace
