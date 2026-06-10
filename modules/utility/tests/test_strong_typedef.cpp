/**
 * @file
 * @brief Tests for nexenne::utility::strong_typedef.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

#include <nexenne/utility/strong_typedef.hpp>

namespace {

namespace util = nexenne::utility;
using util::ability;

using meters =
  util::strong_typedef<struct meters_tag, double, ability::arithmetic | ability::comparable>;
using chip_id = util::strong_typedef<struct chip_tag, std::uint16_t, ability::comparable>;
using reg = util::strong_typedef<
  struct reg_tag,
  std::uint8_t,
  ability::bitwise | ability::shift | ability::bitops | ability::comparable>;
using sat = util::strong_typedef<struct sat_tag, std::uint8_t, ability::saturating>;
using sat_i = util::strong_typedef<struct sat_i_tag, int, ability::saturating>;
using flag = util::strong_typedef<struct flag_tag, int, ability::boolean>;
using ticks = util::strong_typedef<
  struct ticks_tag,
  int,
  ability::arithmetic | ability::comparable | ability::modulo>;
using counts =
  util::strong_typedef<struct counts_tag, unsigned, ability::arithmetic | ability::comparable>;

// Same-tag wrappers over different underlyings: cross-conversion and mixed ops.
struct id_tag;
using id16 = util::strong_typedef<id_tag, std::uint16_t, ability::comparable>;
using id32 = util::strong_typedef<id_tag, std::uint32_t, ability::comparable>;

// Trivially copyable when the underlying is (passes in registers).
static_assert(std::is_trivially_copyable_v<meters>);

// Opt-in safety: distinct tags are unrelated types and never mix.
static_assert(!util::same_tag<meters, chip_id>);
static_assert(util::same_tag<chip_id, chip_id>);

TEST_CASE("nexenne::utility::strong_typedef arithmetic quantity") {
  CHECK((meters{10.0} + meters{5.0}).get() == doctest::Approx(15.0));
  CHECK((meters{10.0} - meters{5.0}).get() == doctest::Approx(5.0));
  CHECK((-meters{10.0}).get() == doctest::Approx(-10.0));
  CHECK((meters{10.0} * 2.0).get() == doctest::Approx(20.0));
  CHECK((2.0 * meters{10.0}).get() == doctest::Approx(20.0));
  CHECK((meters{10.0} / 2.0).get() == doctest::Approx(5.0));
  CHECK((meters{10.0} / meters{2.0}) == doctest::Approx(5.0));  // ratio -> bare double
  CHECK(util::abs(meters{-3.0}).get() == doctest::Approx(3.0));

  auto m{meters{1.0}};
  m += meters{2.0};
  m *= 2.0;
  CHECK(m.get() == doctest::Approx(6.0));
  ++m;
  CHECK(m.get() == doctest::Approx(7.0));
}

TEST_CASE("nexenne::utility::strong_typedef comparison") {
  CHECK(chip_id{1} == chip_id{1});
  CHECK(chip_id{1} != chip_id{2});
  CHECK(chip_id{1} < chip_id{2});
  CHECK(meters{1.0} <= meters{1.0});
}

TEST_CASE("nexenne::utility::strong_typedef register bit operations") {
  CHECK((reg{0b1100} & reg{0b1010}).get() == 0b1000);
  CHECK((reg{0b1100} | reg{0b1010}).get() == 0b1110);
  CHECK((reg{0b1100} ^ reg{0b1010}).get() == 0b0110);
  CHECK((~reg{0x00}).get() == 0xFF);
  CHECK((reg{0b0000'0001} << 2).get() == 0b0000'0100);
  CHECK((reg{0b0000'0100} >> 2).get() == 0b0000'0001);
  CHECK(util::popcount(reg{0b1011}) == 3);
  CHECK(util::bit_width(reg{0b1011}) == 4);
  CHECK(util::rotl(reg{0b1000'0001}, 1).get() == 0b0000'0011);
}

TEST_CASE("nexenne::utility::strong_typedef integer arithmetic: modulo, decrement, unary plus") {
  CHECK((ticks{10} % ticks{3}).get() == 1);
  auto t{ticks{10}};
  t %= ticks{4};
  CHECK(t.get() == 2);

  auto c{ticks{5}};
  CHECK((--c).get() == 4);
  CHECK((c--).get() == 4);  // post-decrement returns the old value
  CHECK(c.get() == 3);
  CHECK((c++).get() == 3);
  CHECK(c.get() == 4);

  CHECK((+ticks{7}).get() == 7);  // unary plus preserves the strong type
  CHECK(util::abs(ticks{-4}).get() == 4);
  CHECK(util::abs(counts{5}).get() == 5);  // unsigned abs is the identity
}

TEST_CASE("nexenne::utility::strong_typedef saturating clamps at both bounds") {
  CHECK(util::sat_add(sat{200}, sat{100}).get() == 255);
  CHECK(util::sat_sub(sat{10}, sat{20}).get() == 0);

  constexpr auto imax{std::numeric_limits<int>::max()};
  constexpr auto imin{std::numeric_limits<int>::min()};
  CHECK(util::sat_add(sat_i{imax}, sat_i{1}).get() == imax);
  CHECK(util::sat_sub(sat_i{imin}, sat_i{1}).get() == imin);
}

TEST_CASE("nexenne::utility::strong_typedef bitwise compound-assign and bit counting") {
  auto r{reg{0b1100}};
  r &= reg{0b1010};
  CHECK(r.get() == 0b1000);
  r |= reg{0b0001};
  CHECK(r.get() == 0b1001);
  r ^= reg{0b1111};
  CHECK(r.get() == 0b0110);
  r <<= 1;
  CHECK(r.get() == 0b1100);
  r >>= 2;
  CHECK(r.get() == 0b0011);

  CHECK((reg{1} << 8).get() == 1);  // shift count normalised modulo width
  CHECK(util::rotr(reg{0b0000'0011}, 1).get() == 0b1000'0001);
  CHECK(util::countl_zero(reg{0b0000'0001}) == 7);
  CHECK(util::countl_one(reg{0b1111'1111}) == 8);
  CHECK(util::countr_zero(reg{0b0000'1000}) == 3);
  CHECK(util::countr_one(reg{0b0000'0111}) == 3);
}

TEST_CASE("nexenne::utility::strong_typedef cross-underlying conversion, mixed ops, swap") {
  id32 const wide{id16{42}};  // explicit same-tag widening
  CHECK(wide.get() == 42);
  CHECK(id16{1} == id32{1});  // mixed-underlying comparison via common_type

  auto a{ticks{1}};
  auto b{ticks{2}};
  using std::swap;
  swap(a, b);
  CHECK(a.get() == 2);
  CHECK(b.get() == 1);
}

TEST_CASE("nexenne::utility::strong_typedef boolean, hash, format, unwrap") {
  CHECK(static_cast<bool>(flag{5}));
  CHECK_FALSE(static_cast<bool>(flag{0}));

  CHECK(std::hash<chip_id>{}(chip_id{42}) == std::hash<std::uint16_t>{}(std::uint16_t{42}));
  CHECK(std::format("{}", chip_id{42}) == "42");
  CHECK(std::format("{:>4}", chip_id{42}) == "  42");  // format spec passes through

  auto id{chip_id{7}};
  util::to_underlying(id) = 9;
  CHECK(id.get() == 9);
}

}  // namespace
