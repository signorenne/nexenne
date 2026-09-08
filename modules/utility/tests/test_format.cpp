/**
 * @file
 * @brief Tests for the nexenne::utility formatters.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <format>

#include <nexenne/utility/format.hpp>

namespace {

namespace util = nexenne::utility;

enum class bit : std::uint8_t {
  a = 1U << 0U,
  b = 1U << 1U,
};

using count = util::strong_typedef<struct count_tag, int>;

TEST_CASE("nexenne::utility::format.hpp alone formats every utility type") {
  CHECK(std::format("{}", util::cobs::error::truncated_input) == "truncated_input");
  CHECK(std::format("{:#x}", util::flags{bit::a} | bit::b) == "0x3");
  CHECK(std::format("{:>4}", util::static_string{"ab"}) == "  ab");
  CHECK(std::format("{:03}", count{7}) == "007");
  CHECK(std::format("{}", util::ability::scale) == "scale");
}

}  // namespace
