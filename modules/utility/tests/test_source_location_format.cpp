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

// Basename reduction (the core path logic), exercised on synthetic paths.
static_assert(util::detail::basename_of("a/b/c.cpp") == "c.cpp");
static_assert(util::detail::basename_of("c.cpp") == "c.cpp");        // no separator
static_assert(util::detail::basename_of("a\\b\\c.cpp") == "c.cpp");  // backslash
static_assert(util::detail::basename_of("a/b/").empty());            // trailing separator
static_assert(util::detail::basename_of("").empty());

TEST_CASE("nexenne::utility::format_short yields exactly basename:line") {
  std::array<char, 256> buf{};
  auto const loc{std::source_location::current()};
  auto const tag{util::format_short(loc, buf)};

  auto const expected{std::string{"test_source_location_format.cpp:"} + std::to_string(loc.line())};
  CHECK(std::string{tag} == expected);
}

TEST_CASE("nexenne::utility::format_long appends the function name") {
  std::array<char, 256> buf{};
  auto const tag{util::format_long(std::source_location::current(), buf)};

  CHECK(tag.find("test_source_location_format.cpp:") == 0);
  CHECK(tag.find(" in ") != std::string_view::npos);
}

TEST_CASE("nexenne::utility formatters truncate into a tiny buffer") {
  std::array<char, 4> small{};
  CHECK(util::format_short(std::source_location::current(), small).size() <= 3);

  std::array<char, 6> medium{};
  CHECK(util::format_long(std::source_location::current(), medium).size() <= 5);

  std::array<char, 1> one{};
  CHECK(util::format_short(std::source_location::current(), one).empty());
}

}  // namespace
