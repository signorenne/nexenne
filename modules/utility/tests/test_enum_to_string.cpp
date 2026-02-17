/**
 * @file
 * @brief Tests for the nexenne::utility enum reflection helpers.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

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

static_assert(util::enumeration<color>);
static_assert(util::enumeration<signed_e>);
static_assert(!util::enumeration<int>);
static_assert(!util::enumeration<color*>);

struct not_enum {};

static_assert(!util::enumeration<not_enum>);

static_assert(util::enum_name<color::blue>() == "blue");
static_assert(util::enum_name<gap_e::a>() == "a");
static_assert(util::enum_name<gap_e::c>() == "c");
static_assert(util::enum_name<signed_e::neg>() == "neg");
static_assert(util::enum_name<signed_e::zero>() == "zero");
static_assert(util::enum_name<signed_e::pos>() == "pos");
static_assert(util::enum_name<big_e::small>() == "small");
static_assert(util::enum_name<big_e::large>() == "large");
static_assert(util::enum_name<unscoped_e::ux>() == "ux");
static_assert(util::enum_name<unscoped_e::uy>() == "uy");

// A literal value with no matching enumerator must yield an empty spelling
// (the compiler renders a "(E)N" placeholder which the parser rejects).
static_assert(util::enum_name<static_cast<color>(99)>().empty());
static_assert(util::enum_name<static_cast<gap_e>(1)>().empty());      // the hole
static_assert(util::enum_name<static_cast<signed_e>(-5)>().empty());  // negative placeholder

static_assert(util::enum_count<color>() == 3);
static_assert(util::enum_count<unscoped_e>() == 2);
static_assert(util::enum_count<gap_e>() == 2);  // hole not counted
// big_e::large at 300 is outside the default window; only `small` is counted.
static_assert(util::enum_count<big_e>() == 1);
static_assert(util::enum_count<big_e, 512>() == 2);  // widen to see `large`
// A tiny window clips the census.
static_assert(util::enum_count<color, 2>() == 2);  // only red, green in [0,2)
static_assert(util::enum_count<color, 0>() == 0);  // empty window

static_assert(util::enum_values<color>().size() == 3);
static_assert(util::enum_values<signed_e, 256, -1>().size() == 3);
static_assert(
  util::enum_values<signed_e, 256, -1>() == std::array{signed_e::neg, signed_e::zero, signed_e::pos}
);
static_assert(util::enum_values<big_e, 512>() == std::array{big_e::small, big_e::large});
static_assert(util::enum_values<color, 0>().size() == 0);  // empty window: empty array
static_assert(util::enum_values<unscoped_e>() == std::array{unscoped_e::ux, unscoped_e::uy});

static_assert(util::enum_to_string(color::red) == "red");
static_assert(util::enum_to_string(color::green) == "green");
static_assert(util::enum_to_string(color::blue) == "blue");
static_assert(util::enum_to_string(signed_e::zero) == "zero");
static_assert(util::enum_to_string<256, -1>(signed_e::pos) == "pos");
static_assert(util::enum_to_string(static_cast<color>(7)).empty());  // unnamed in range
static_assert(util::enum_to_string(big_e::small) == "small");

static_assert(util::enum_cast<color>("red") == color::red);
static_assert(util::enum_cast<color>("green") == color::green);
static_assert(util::enum_cast<color>("blue") == color::blue);
static_assert(util::enum_cast<color>("nope") == std::nullopt);
static_assert(util::enum_cast<color>("") == std::nullopt);     // empty never matches
static_assert(util::enum_cast<color>("RED") == std::nullopt);  // case-sensitive
static_assert(util::enum_cast<signed_e, 256, -1>("neg") == signed_e::neg);
static_assert(util::enum_cast<big_e>("large") == std::nullopt);  // 300 out of default window
static_assert(util::enum_cast<big_e, 512>("large") == big_e::large);

static_assert(util::enum_to_string(util::enum_cast<color>("blue").value()) == "blue");
static_assert(util::enum_cast<color>(util::enum_to_string(color::green)).value() == color::green);

TEST_CASE("nexenne::utility enum value-to-name and name-to-value") {
  CHECK(util::enum_to_string(color::blue) == "blue");
  CHECK(util::enum_to_string(static_cast<color>(99)).empty());

  CHECK(util::enum_cast<color>("red") == color::red);
  CHECK_FALSE(util::enum_cast<color>("nope").has_value());
  CHECK(util::enum_cast<color>("") == std::nullopt);  // empty name never matches
  CHECK(util::enum_to_string(unscoped_e::uy) == "uy");
}

TEST_CASE("nexenne::utility::enum_to_string names every enumerator at runtime") {
  CHECK(util::enum_to_string(color::red) == "red");
  CHECK(util::enum_to_string(color::green) == "green");
  CHECK(util::enum_to_string(color::blue) == "blue");

  CHECK(util::enum_to_string(gap_e::a) == "a");
  CHECK(util::enum_to_string(gap_e::c) == "c");
  CHECK(util::enum_to_string(static_cast<gap_e>(1)).empty());  // the hole

  CHECK(util::enum_to_string<256, -1>(signed_e::neg) == "neg");
  CHECK(util::enum_to_string<256, -1>(signed_e::zero) == "zero");
  CHECK(util::enum_to_string<256, -1>(signed_e::pos) == "pos");

  CHECK(util::enum_to_string(big_e::large).empty());  // out of default window
  CHECK(util::enum_to_string<512>(big_e::large) == "large");
}

TEST_CASE("nexenne::utility::enum_cast resolves and rejects names at runtime") {
  CHECK(util::enum_cast<color>("green") == color::green);
  CHECK(util::enum_cast<color>("blue") == color::blue);
  CHECK_FALSE(util::enum_cast<color>("gree").has_value());  // prefix is not a match
  CHECK_FALSE(util::enum_cast<color>("greenish").has_value());

  CHECK(util::enum_cast<signed_e, 256, -1>("neg") == signed_e::neg);
  CHECK(util::enum_cast<big_e, 512>("large") == big_e::large);
  CHECK_FALSE(util::enum_cast<big_e>("large").has_value());  // out of default window
}

TEST_CASE("nexenne::utility enum round-trips through name and back") {
  for (auto const value : util::enum_values<color>()) {
    auto const name{util::enum_to_string(value)};
    CHECK_FALSE(name.empty());
    CHECK(util::enum_cast<color>(name) == value);
  }
}

TEST_CASE("nexenne::utility::enum_values is sized and ordered at runtime") {
  auto const values{util::enum_values<color>()};
  CHECK(values.size() == 3);
  CHECK(values[0] == color::red);
  CHECK(values[1] == color::green);
  CHECK(values[2] == color::blue);

  auto const shifted{util::enum_values<signed_e, 256, -1>()};
  CHECK(shifted.size() == 3);
  CHECK(shifted[0] == signed_e::neg);
  CHECK(shifted[2] == signed_e::pos);
}

}  // namespace
