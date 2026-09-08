/**
 * @file
 * @brief Tests for the nexenne::utility formatters.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <sstream>
#include <string>
#include <string_view>

#include <nexenne/utility/format.hpp>

namespace {

namespace util = nexenne::utility;

enum class bit : std::uint8_t {
  a = 1U << 0U,
  b = 1U << 1U,
};

enum class led : std::uint8_t {
  red = 1U << 0U,
  green = 1U << 2U,
};

[[nodiscard]] constexpr auto to_string(led const l) noexcept -> std::string_view {
  return l == led::red ? "red" : "green";
}

using count = util::strong_typedef<struct count_tag, int>;

TEST_CASE("nexenne::utility::format.hpp alone formats every utility type") {
  CHECK(std::format("{}", util::cobs::error::truncated_input) == "truncated_input");
  CHECK(std::format("{:#x}", util::flags{bit::a} | bit::b) == "0x3");
  CHECK(std::format("{:>4}", util::static_string{"ab"}) == "  ab");
  CHECK(std::format("{:03}", count{7}) == "007");
  CHECK(std::format("{}", util::ability::scale) == "scale");
}

TEST_CASE(
  "nexenne::utility::format.hpp gives every utility type to_string and operator<< (utility-20)"
) {
  CHECK(util::to_string(util::flags{bit::a} | bit::b) == "3");
  CHECK(util::to_string(util::static_string{"ab"}) == std::string{"ab"});
  CHECK(util::to_string(count{7}) == "7");

  auto os{std::ostringstream{}};
  os << util::cobs::error::truncated_input << ' ' << (util::flags{bit::a} | bit::b) << ' '
     << util::static_string{"ab"} << ' ' << count{7} << ' ' << util::ability::scale;
  CHECK(os.str() == "truncated_input 3 ab 7 scale");
}

TEST_CASE("nexenne::utility::flags prints a named enum's set by its bit names") {
  auto const both{util::flags{led::red} | led::green};
  CHECK(std::format("{}", both) == "red green");
  CHECK(std::format("{:>10}", both) == " red green");
  CHECK(util::to_string(both) == "red green");
  CHECK(util::to_string(util::flags<led>{}).empty());
  auto os{std::ostringstream{}};
  os << both;
  CHECK(os.str() == "red green");
  CHECK(std::format("{:#x}", both.raw()) == "0x5");
  CHECK(std::format("{}", util::flags{bit::a} | bit::b) == "3");
}

}  // namespace
