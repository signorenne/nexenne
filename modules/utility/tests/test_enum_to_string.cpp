/**
 * @file
 * @brief Tests for the nexenne::utility enum reflection helpers.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>

#include <nexenne/utility/enum_to_string.hpp>

namespace {

namespace util = nexenne::utility;

enum class color : std::uint8_t {
  red,
  green,
  blue
};

enum class gap_e : std::uint8_t {
  a = 0,
  c = 2  // hole at value 1
};

enum class signed_e : int {
  neg = -1,
  zero = 0,
  pos = 1
};

enum class big_e : std::uint16_t {
  small = 1,
  large = 300  // beyond the default [0, 256) window
};

// Unscoped, but with a fixed underlying type so the scan window stays in range.
enum unscoped_e : unsigned {
  ux,
  uy
};

static_assert(util::enum_name<color::green>() == "green");
static_assert(util::enum_name<color::red>() == "red");  // value 0
static_assert(util::enum_count<color>() == 3);
static_assert(util::enum_values<color>() == std::array{color::red, color::green, color::blue});

// Holes are skipped and not counted; the array packs contiguously.
static_assert(util::enum_count<gap_e>() == 2);
static_assert(util::enum_to_string(static_cast<gap_e>(1)).empty());
static_assert(util::enum_values<gap_e>() == std::array{gap_e::a, gap_e::c});

// Negative enumerators need a shifted window.
static_assert(util::enum_to_string<256, -1>(signed_e::neg) == "neg");
static_assert((util::enum_count<signed_e, 256, -1>()) == 3);

// Values beyond the default window are invisible until the range is widened.
static_assert(util::enum_to_string(big_e::large).empty());
static_assert(util::enum_to_string<512>(big_e::large) == "large");

// Unscoped enums reflect too.
static_assert(util::enumeration<unscoped_e>);

TEST_CASE("nexenne::utility enum value-to-name and name-to-value") {
  CHECK(util::enum_to_string(color::blue) == "blue");
  CHECK(util::enum_to_string(static_cast<color>(99)).empty());

  CHECK(util::enum_cast<color>("red") == color::red);
  CHECK_FALSE(util::enum_cast<color>("nope").has_value());
  CHECK(util::enum_cast<color>("") == std::nullopt);  // empty name never matches
  CHECK(util::enum_to_string(unscoped_e::uy) == "uy");
}

}  // namespace
