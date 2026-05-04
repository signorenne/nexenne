/**
 * @file
 * @brief Tests for nexenne::utility::strong_typedef.
 */

#include <doctest/doctest.h>

#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
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

// A strong type over a non-trivial underlying (std::string): exercises move,
// comparison, hashing and formatting on something that is not a scalar.
using name = util::strong_typedef<struct name_tag, std::string, ability::comparable>;

// A wrapper with no capabilities at all: only construction, value access,
// hashing and formatting are available.
using opaque = util::strong_typedef<struct opaque_tag, int>;

// The ready-made profiles.
using length = util::quantity<struct length_tag, double>;
using token = util::identifier<struct token_tag, std::uint32_t>;
using seq = util::counter<struct seq_tag, int>;
using mask = util::bitfield<struct mask_tag, std::uint16_t>;

// A wrapper over bool: ability::boolean must NOT add operator bool when the
// underlying is already bool (avoiding an ambiguous double-conversion).
using bit = util::strong_typedef<struct bit_tag, bool, ability::boolean>;

// Trivially copyable when the underlying is (passes in registers).
static_assert(std::is_trivially_copyable_v<meters>);
static_assert(std::is_trivially_copyable_v<chip_id>);
static_assert(!std::is_trivially_copyable_v<name>);  // std::string is not

// Opt-in safety: distinct tags are unrelated types and never mix.
static_assert(!util::same_tag_as<meters, chip_id>);
static_assert(util::same_tag_as<chip_id, chip_id>);
static_assert(util::same_tag_as<id16, id32>);
static_assert(util::strong_typedef_like<meters>);
static_assert(util::strong_typedef_like<meters const&>);  // cv/ref variants
static_assert(!util::strong_typedef_like<double>);
static_assert(!util::strong_typedef_like<int>);

// Two strong typedefs over the SAME underlying but DIFFERENT tags are distinct,
// unrelated types: neither implicitly converts to the other, and neither
// implicitly converts to the shared underlying.
using alpha = util::strong_typedef<struct alpha_tag, int, ability::arithmetic>;
using beta = util::strong_typedef<struct beta_tag, int, ability::arithmetic>;
static_assert(!std::is_same_v<alpha, beta>);
static_assert(!std::is_convertible_v<alpha, beta>);
static_assert(!std::is_convertible_v<beta, alpha>);
static_assert(!std::is_convertible_v<alpha, int>);  // operator T is explicit
static_assert(!std::is_convertible_v<int, alpha>);  // value ctor is explicit
static_assert(!std::is_convertible_v<int, opaque>);
static_assert(!util::same_tag_as<alpha, beta>);

// The value constructor is explicit; the underlying-conversion operator too.
static_assert(std::is_constructible_v<alpha, int>);
static_assert(std::is_constructible_v<int, alpha>);  // explicit operator T

// tag_type / value_type / abilities surface.
static_assert(std::is_same_v<meters::value_type, double>);
static_assert(std::is_same_v<chip_id::value_type, std::uint16_t>);
static_assert(meters::abilities == (ability::arithmetic | ability::comparable));
static_assert(opaque::abilities == ability::none);

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

  CHECK((reg{1} << 7).get() == 0x80);  // width-1 is the largest valid shift count
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

// ability bitmask algebra

TEST_CASE("nexenne::utility::ability bitmask union, intersection, complement, has") {
  constexpr auto both{ability::add | ability::subtract};
  CHECK(util::has(both, ability::add));
  CHECK(util::has(both, ability::subtract));
  CHECK_FALSE(util::has(both, ability::scale));

  // has() with a group checks every bit of the group is present.
  CHECK(util::has(ability::arithmetic, ability::add));
  CHECK(util::has(ability::arithmetic, ability::scale));
  CHECK_FALSE(util::has(ability::add, ability::arithmetic));  // group not subset of one flag
  CHECK(util::has(ability::comparable, ability::equality | ability::ordered));

  // Intersection keeps only common flags.
  constexpr auto inter{(ability::add | ability::scale) & (ability::scale | ability::ratio)};
  CHECK(inter == ability::scale);

  // none is the identity for union and the absorber for intersection.
  CHECK((ability::add | ability::none) == ability::add);
  CHECK((ability::add & ability::none) == ability::none);

  // Complement then intersect clears a flag.
  constexpr auto cleared{ability::arithmetic & ~ability::scale};
  CHECK_FALSE(util::has(cleared, ability::scale));
  CHECK(util::has(cleared, ability::add));
}

TEST_CASE("nexenne::utility::sanitized drops unsigned-only flags off non-unsigned T") {
  constexpr auto wanted{ability::shift | ability::bitops | ability::add};

  // Unsigned underlying: nothing is dropped.
  CHECK(util::sanitized<std::uint32_t>(wanted) == wanted);

  // Signed underlying: shift and bitops are removed, add survives.
  constexpr auto on_signed{util::sanitized<int>(wanted)};
  CHECK_FALSE(util::has(on_signed, ability::shift));
  CHECK_FALSE(util::has(on_signed, ability::bitops));
  CHECK(util::has(on_signed, ability::add));

  // Floating underlying: likewise stripped.
  constexpr auto on_double{util::sanitized<double>(wanted)};
  CHECK_FALSE(util::has(on_double, ability::shift));
  CHECK(util::has(on_double, ability::add));
}

// the ready-made profiles

TEST_CASE("nexenne::utility::strong_typedef ready-made profiles expose the right abilities") {
  // quantity: full numeric surface.
  CHECK((length{4.0} + length{2.0}).get() == doctest::Approx(6.0));
  CHECK((length{4.0} * 3.0).get() == doctest::Approx(12.0));
  CHECK(length{1.0} < length{2.0});

  // identifier: comparable + hashable, no arithmetic.
  CHECK(token{1} == token{1});
  CHECK(token{1} != token{2});
  static_assert(!util::detail::has_op<token, ability::add>);

  // counter: comparable plus inc/dec, no arithmetic add.
  auto s{seq{0}};
  ++s;
  ++s;
  CHECK(s.get() == 2);
  --s;
  CHECK(s.get() == 1);
  CHECK(seq{1} == s);
  static_assert(!util::detail::has_op<seq, ability::add>);
  static_assert(util::detail::has_op<seq, ability::increment>);

  // bitfield: bitwise, shift, bitops, comparable on unsigned.
  CHECK((mask{0xF0} | mask{0x0F}).get() == 0xFF);
  CHECK((mask{0x01} << 4).get() == 0x10);
  CHECK(util::popcount(mask{0xFFFF}) == 16);
  CHECK(mask{1} != mask{2});
}

// non-trivial underlying (std::string)

TEST_CASE("nexenne::utility::strong_typedef over std::string: compare, move, hash, format") {
  name const a{std::string{"alice"}};
  name const b{std::string{"bob"}};
  CHECK(a == a);
  CHECK(a != b);
  CHECK(a < b);  // lexicographic ordering of the underlying string
  CHECK(b > a);

  // Move-construct the underlying through the value constructor.
  std::string source{"carol"};
  name moved{std::move(source)};
  CHECK(moved.get() == "carol");

  // Hash forwards to std::hash<std::string>.
  CHECK(std::hash<name>{}(a) == std::hash<std::string>{}(std::string{"alice"}));

  // Format forwards to the string formatter, including specs.
  CHECK(std::format("{}", a) == "alice");
  CHECK(std::format("{:>7}", a) == "  alice");

  // Mutable get() allows editing the wrapped string in place.
  name editable{std::string{"x"}};
  editable.get() += "yz";
  CHECK(editable.get() == "xyz");
}

// distinctness / overload removal (negative capabilities)

TEST_CASE("nexenne::utility::strong_typedef absent capabilities are removed from overloads") {
  // chip_id is comparable-only: no arithmetic, scale, bitwise, increment, etc.
  static_assert(!util::detail::has_op<chip_id, ability::add>);
  static_assert(!util::detail::has_op<chip_id, ability::scale>);
  static_assert(!util::detail::has_op<chip_id, ability::increment>);
  static_assert(!util::detail::has_op<chip_id, ability::bit_and>);
  static_assert(util::detail::has_op<chip_id, ability::equality>);
  static_assert(util::detail::has_op<chip_id, ability::ordered>);

  // opaque has nothing: not even equality.
  static_assert(!util::detail::has_op<opaque, ability::equality>);
  static_assert(!util::detail::has_op<opaque, ability::ordered>);
  static_assert(!util::detail::has_op<opaque, ability::add>);

  // Helpers that prove an operator does NOT participate in overload resolution.
  auto const has_plus{[]<typename U>(U) { return requires(U x) { x + x; }; }};
  auto const has_mul_scalar{[]<typename U>(U) { return requires(U x) { x * 2; }; }};
  auto const has_inc{[]<typename U>(U) { return requires(U x) { ++x; }; }};
  auto const has_eq{[]<typename U>(U) { return requires(U x) { x == x; }; }};
  auto const has_less{[]<typename U>(U) { return requires(U x) { x < x; }; }};
  auto const has_and{[]<typename U>(U) { return requires(U x) { x & x; }; }};

  CHECK(has_plus(meters{0.0}));
  CHECK_FALSE(has_plus(chip_id{0}));
  CHECK_FALSE(has_mul_scalar(chip_id{0}));
  CHECK_FALSE(has_inc(chip_id{0}));
  CHECK(has_eq(chip_id{0}));
  CHECK(has_less(chip_id{0}));

  CHECK_FALSE(has_eq(opaque{0}));
  CHECK_FALSE(has_less(opaque{0}));
  CHECK_FALSE(has_plus(opaque{0}));

  CHECK(has_and(reg{0}));
  CHECK_FALSE(has_and(meters{0.0}));  // bitwise not enabled (and double anyway)
}

// default construction & explicit construction

TEST_CASE("nexenne::utility::strong_typedef default construction value-initialises") {
  CHECK(meters{}.get() == doctest::Approx(0.0));
  CHECK(chip_id{}.get() == 0);
  CHECK(opaque{}.get() == 0);
  CHECK(name{}.get().empty());

  static_assert(std::is_default_constructible_v<meters>);
  static_assert(std::is_nothrow_default_constructible_v<chip_id>);
}

// comparison & ordering depth

TEST_CASE("nexenne::utility::strong_typedef ordering yields the full relational set") {
  CHECK(meters{1.0} < meters{2.0});
  CHECK(meters{2.0} > meters{1.0});
  CHECK(meters{1.0} <= meters{1.0});
  CHECK(meters{1.0} >= meters{1.0});
  CHECK_FALSE(meters{2.0} < meters{1.0});

  // Three-way comparison returns the underlying's ordering category.
  CHECK((meters{1.0} <=> meters{2.0}) == std::partial_ordering::less);
  CHECK((chip_id{2} <=> chip_id{1}) == std::strong_ordering::greater);
  CHECK((chip_id{1} <=> chip_id{1}) == std::strong_ordering::equal);
  static_assert(std::is_same_v<decltype(chip_id{0} <=> chip_id{0}), std::strong_ordering>);
  static_assert(std::is_same_v<decltype(meters{0.0} <=> meters{0.0}), std::partial_ordering>);
}

TEST_CASE("nexenne::utility::strong_typedef mixed-underlying comparison via common_type") {
  CHECK(id16{5} == id32{5});
  CHECK(id16{5} != id32{6});
  CHECK(id16{5} < id32{9});
  CHECK(id32{9} > id16{5});
  CHECK((id16{5} <=> id32{5}) == std::strong_ordering::equal);
}

// arithmetic result-type & fold-back behaviour

TEST_CASE("nexenne::utility::strong_typedef same-tag arithmetic keeps the strong type") {
  static_assert(std::is_same_v<decltype(meters{1.0} + meters{2.0}), meters>);
  static_assert(std::is_same_v<decltype(meters{1.0} - meters{2.0}), meters>);
  static_assert(std::is_same_v<decltype(-meters{1.0}), meters>);
  static_assert(std::is_same_v<decltype(meters{1.0} * 2.0), meters>);
  static_assert(std::is_same_v<decltype(2.0 * meters{1.0}), meters>);
  static_assert(std::is_same_v<decltype(meters{1.0} / 2.0), meters>);

  // ratio yields a bare scalar (the common underlying), not a strong type.
  static_assert(std::is_same_v<decltype(meters{4.0} / meters{2.0}), double>);
  static_assert(std::is_same_v<decltype(ticks{4} / ticks{2}), int>);
  CHECK((ticks{9} / ticks{2}) == 4);  // integer division
}

TEST_CASE("nexenne::utility::strong_typedef scale folds back into the underlying") {
  // ticks is int: scaling stays integral and truncates, mirroring operator*=.
  CHECK((ticks{7} * 2).get() == 14);
  CHECK((ticks{7} / 2).get() == 3);  // integer division, folded back to int
  auto t{ticks{10}};
  t /= 3;
  CHECK(t.get() == 3);
  t *= 4;
  CHECK(t.get() == 12);
}

TEST_CASE("nexenne::utility::strong_typedef mixed-underlying arithmetic picks the common type") {
  using w16 = util::strong_typedef<struct w_tag, std::uint16_t, ability::arithmetic>;
  using w32 = util::strong_typedef<struct w_tag, std::uint32_t, ability::arithmetic>;
  auto const sum{w16{40000} + w32{40000}};
  // common_type of uint16/uint32 promotes; the sum does not wrap at 16 bits.
  static_assert(std::is_same_v<decltype(sum)::value_type, std::uint32_t>);
  CHECK(sum.get() == 80000U);
}

// compound assignment returns *this

TEST_CASE("nexenne::utility::strong_typedef compound assignment chains and returns self") {
  auto m{meters{1.0}};
  auto& ref{m += meters{2.0}};
  CHECK(&ref == &m);
  CHECK(m.get() == doctest::Approx(3.0));

  m -= meters{1.0};
  CHECK(m.get() == doctest::Approx(2.0));
  (m *= 5.0) /= 2.0;
  CHECK(m.get() == doctest::Approx(5.0));
}

// unsigned bitops edge cases

TEST_CASE("nexenne::utility::strong_typedef shift boundary counts and bit-counting edges") {
  // Boundary shift counts: 0 is a no-op and width-1 is the largest valid count.
  CHECK((reg{0xAB} << 0).get() == 0xAB);
  CHECK((reg{0xAB} >> 0).get() == 0xAB);
  CHECK((reg{1} << 7).get() == 0x80);
  CHECK((reg{0x80} >> 7).get() == 1);

  // bit_width of zero is 0; popcount of all-ones is the width.
  CHECK(util::bit_width(reg{0}) == 0);
  CHECK(util::popcount(reg{0xFF}) == 8);
  CHECK(util::popcount(reg{0}) == 0);

  // countl/countr on the extremes.
  CHECK(util::countl_zero(reg{0}) == 8);
  CHECK(util::countr_zero(reg{0}) == 8);
  CHECK(util::countl_one(reg{0}) == 0);
  CHECK(util::countr_one(reg{0xFF}) == 8);

  // rotl/rotr by the full width is the identity.
  CHECK(util::rotl(reg{0b1011'0010}, 8).get() == 0b1011'0010);
  CHECK(util::rotr(reg{0b1011'0010}, 8).get() == 0b1011'0010);
  // rotl by k then rotr by k restores the value.
  CHECK(util::rotr(util::rotl(reg{0b0110'1001}, 3), 3).get() == 0b0110'1001);
}

// saturating extra coverage

TEST_CASE("nexenne::utility::strong_typedef saturating in-range and signed underflow/overflow") {
  // In-range saturating ops behave like ordinary ops.
  CHECK(util::sat_add(sat{100}, sat{50}).get() == 150);
  CHECK(util::sat_sub(sat{100}, sat{50}).get() == 50);

  constexpr auto imax{std::numeric_limits<int>::max()};
  constexpr auto imin{std::numeric_limits<int>::min()};
  // Signed: subtracting a negative can overflow upward; adding a negative is fine.
  CHECK(util::sat_sub(sat_i{imax}, sat_i{-1}).get() == imax);
  CHECK(util::sat_add(sat_i{imin}, sat_i{-1}).get() == imin);
  CHECK(util::sat_add(sat_i{5}, sat_i{-3}).get() == 2);

  // sat_add/sat_sub are constexpr.
  constexpr auto clamped{util::sat_add(sat{255}, sat{255})};
  static_assert(clamped.get() == 255);
}

// boolean conversion edge: bool underlying does not gain operator bool

TEST_CASE("nexenne::utility::strong_typedef boolean conversion semantics") {
  CHECK(static_cast<bool>(flag{5}));
  CHECK_FALSE(static_cast<bool>(flag{0}));
  // The conversion is explicit: no implicit bool slide.
  static_assert(!std::is_convertible_v<flag, bool>);
  static_assert(std::is_constructible_v<bool, flag>);  // via explicit operator

  // A wrapper whose underlying is bool gets bool conversion only through the
  // generic explicit operator T const& (T == bool); the extra operator bool is
  // suppressed (T == bool excluded) so there is exactly one, unambiguous path.
  static_assert(std::is_constructible_v<bool, bit>);  // via explicit operator bool const&
  static_assert(!std::is_convertible_v<bit, bool>);   // but still explicit
  CHECK(static_cast<bool>(bit{true}));
  CHECK_FALSE(static_cast<bool>(bit{false}));

  // flag (int underlying, ability::boolean) is truthy on non-zero values.
  CHECK_FALSE(static_cast<bool>(flag{0}));
  CHECK(static_cast<bool>(flag{-1}));
}

// explicit underlying conversion

TEST_CASE("nexenne::utility::strong_typedef explicit conversion to underlying") {
  meters const m{12.5};
  auto const raw{static_cast<double>(m)};
  CHECK(raw == doctest::Approx(12.5));

  // to_underlying overloads: const yields const ref, mutable yields mutable ref.
  static_assert(std::is_same_v<decltype(util::to_underlying(std::declval<meters&>())), double&>);
  static_assert(std::is_same_v<
                decltype(util::to_underlying(std::declval<meters const&>())),
                double const&>);
}

// hashing in an unordered container

TEST_CASE("nexenne::utility::strong_typedef keys an unordered_map") {
  std::unordered_map<chip_id, std::string> by_id;
  by_id[chip_id{1}] = "one";
  by_id[chip_id{2}] = "two";
  CHECK(by_id.at(chip_id{1}) == "one");
  CHECK(by_id.at(chip_id{2}) == "two");
  CHECK(by_id.size() == 2);

  // The hash is noexcept when the underlying hash is.
  static_assert(noexcept(std::hash<chip_id>{}(chip_id{0})));
}

// std::common_type specialisation

TEST_CASE("nexenne::utility::strong_typedef common_type unions underlying and abilities") {
  using common = std::common_type_t<id16, id32>;
  static_assert(std::is_same_v<common::value_type, std::uint32_t>);
  static_assert(std::is_same_v<common::tag_type, id_tag>);
  // Abilities are the union of both operand sets.
  static_assert(util::has(common::abilities, ability::equality));
  static_assert(util::has(common::abilities, ability::ordered));
  CHECK(true);
}

// constexpr usage of the whole surface

TEST_CASE("nexenne::utility::strong_typedef is fully usable at compile time") {
  constexpr auto total{meters{10.0} + meters{5.0}};
  static_assert(total.get() == 15.0);
  static_assert((-meters{3.0}).get() == -3.0);
  static_assert((meters{8.0} / meters{2.0}) == 4.0);
  static_assert((meters{3.0} * 4.0).get() == 12.0);

  constexpr auto anded{reg{0b1100} & reg{0b1010}};
  static_assert(anded.get() == 0b1000);
  static_assert((reg{1} << 3).get() == 0b1000);
  static_assert(util::popcount(reg{0b1011}) == 3);

  static_assert(chip_id{1} == chip_id{1});
  static_assert(chip_id{1} < chip_id{2});

  // constexpr swap.
  constexpr auto swapped{[] {
    auto a{ticks{1}};
    auto b{ticks{2}};
    using std::swap;
    swap(a, b);
    return a.get();
  }()};
  static_assert(swapped == 2);
}

// throwing underlying: conditional noexcept on the mixin operators

// A strong type over std::string with ability::add: concatenation is enabled,
// but string concatenation may allocate, so the operators must be potentially
// throwing rather than terminating on a failed allocation.
using strval =
  util::strong_typedef<struct strval_tag, std::string, ability::add | ability::comparable>;

TEST_CASE("nexenne::utility::strong_typedef over a throwing underlying is conditionally noexcept") {
  // += concatenates, and because std::string concatenation can throw, the
  // compound assignment is NOT noexcept (an unconditional noexcept would turn a
  // bad_alloc into std::terminate).
  static_assert(!noexcept(std::declval<strval&>() += std::declval<strval const&>()));
  // Trivial underlyings keep their noexcept operators.
  static_assert(noexcept(std::declval<meters&>() += std::declval<meters const&>()));

  strval a{std::string{"foo"}};
  a += strval{std::string{"bar"}};
  CHECK(a.get() == "foobar");

  // The free operator+ concatenates too and returns the same strong type.
  auto const joined{strval{std::string{"ab"}} + strval{std::string{"cd"}}};
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(joined)>, strval>);
  CHECK(joined.get() == "abcd");
  CHECK(strval{std::string{"x"}} != strval{std::string{"y"}});
}

// rotate direction with negative counts (rotations keep modulo normalisation)

TEST_CASE("nexenne::utility::strong_typedef rotate accepts negative counts") {
  // A negative rotate count rotates the other way: rotl by -1 == rotr by 1.
  CHECK(util::rotl(reg{0b0110'1001}, -1).get() == util::rotr(reg{0b0110'1001}, 1).get());
  CHECK(util::rotr(reg{0b0110'1001}, -1).get() == util::rotl(reg{0b0110'1001}, 1).get());
}

// same-tag conversions: narrowing underlying and changing the ability set

struct conv_tag;
using conv_lean = util::strong_typedef<conv_tag, int, ability::comparable>;
using conv_rich = util::strong_typedef<conv_tag, int, ability::arithmetic | ability::comparable>;

TEST_CASE("nexenne::utility::strong_typedef same-tag conversions narrow and reshape abilities") {
  // Same tag, different underlying: an explicit narrowing conversion.
  id16 const narrow{id32{70000}};
  CHECK(narrow.get() == static_cast<std::uint16_t>(70000));

  // Same tag, same underlying, DIFFERENT ability set: now explicitly convertible.
  conv_rich const rich{42};
  conv_lean const lean{rich};
  CHECK(lean.get() == 42);
  static_assert(std::is_constructible_v<conv_lean, conv_rich>);
  static_assert(std::is_constructible_v<conv_rich, conv_lean>);
  static_assert(!std::is_convertible_v<conv_rich, conv_lean>);  // still explicit
}

// capability granularity: ordered alone does not grant equality

using eq_only = util::strong_typedef<struct eq_only_tag, int, ability::equality>;
using ord_only = util::strong_typedef<struct ord_only_tag, int, ability::ordered>;

TEST_CASE("nexenne::utility::strong_typedef equality and ordering are independent capabilities") {
  auto const has_eq{[]<typename U>(U) { return requires(U x) { x == x; }; }};
  auto const has_lt{[]<typename U>(U) { return requires(U x) { x < x; }; }};

  // equality-only: has ==, but no relational operators.
  CHECK(has_eq(eq_only{0}));
  CHECK_FALSE(has_lt(eq_only{0}));

  // ordered-only: has the relational operators, but NOT == (it is a separate
  // capability, never synthesised from operator<=>).
  CHECK(has_lt(ord_only{0}));
  CHECK_FALSE(has_eq(ord_only{0}));

  CHECK(eq_only{1} == eq_only{1});
  CHECK(ord_only{1} < ord_only{2});
}

// abs of negative zero clears the sign (std::abs path for floating point)

TEST_CASE("nexenne::utility::strong_typedef abs clears the sign of negative zero") {
  auto const zero{util::abs(meters{-0.0})};
  CHECK(std::signbit(zero.get()) == false);
  CHECK(zero.get() == doctest::Approx(0.0));
  // Ordinary magnitudes are unchanged.
  CHECK(util::abs(meters{-2.5}).get() == doctest::Approx(2.5));
}

// moving the underlying out of an rvalue wrapper via get() &&

TEST_CASE("nexenne::utility::strong_typedef get on an rvalue moves the underlying out") {
  static_assert(std::is_same_v<decltype(std::declval<name>().get()), std::string&&>);
  static_assert(std::is_same_v<decltype(std::declval<name const>().get()), std::string const&&>);

  name src{std::string(64, 'x')};  // long enough to be heap-allocated
  std::string const moved{std::move(src).get()};
  CHECK(moved.size() == 64);
  CHECK(src.get().empty());  // libstdc++ leaves a moved-from string empty
}

// saturating across mixed underlyings promotes to the common strong type

TEST_CASE("nexenne::utility::strong_typedef saturating promotes mixed underlyings") {
  struct smix_tag;
  using s16 = util::strong_typedef<smix_tag, std::uint16_t, ability::saturating>;
  using s32 = util::strong_typedef<smix_tag, std::uint32_t, ability::saturating>;

  auto const summed{util::sat_add(s16{60000}, s32{10000})};
  // The common underlying is uint32, so the sum does not saturate at 16 bits.
  static_assert(std::is_same_v<decltype(summed)::value_type, std::uint32_t>);
  CHECK(summed.get() == 70000U);

  auto const diff{util::sat_sub(s16{5}, s32{9})};
  static_assert(std::is_same_v<decltype(diff)::value_type, std::uint32_t>);
  CHECK(diff.get() == 0U);  // clamps to zero
}

// a mixed operator that rebinds to a signed common type drops shift and bitops

TEST_CASE("nexenne::utility::strong_typedef sanitized drops unsigned-only ops on a mixed result") {
  struct mix_tag;
  using u32reg = util::
    strong_typedef<mix_tag, std::uint32_t, ability::add | ability::shift | ability::bitops>;
  using i64q = util::strong_typedef<mix_tag, std::int64_t, ability::add>;

  using sum_t = decltype(u32reg{5} + i64q{7});
  // The common underlying is signed, so the unsigned-only flags are sanitised away.
  static_assert(std::is_same_v<sum_t::value_type, std::int64_t>);
  static_assert(util::detail::has_op<sum_t, ability::add>);
  static_assert(!util::detail::has_op<sum_t, ability::shift>);
  static_assert(!util::detail::has_op<sum_t, ability::bitops>);
  CHECK((u32reg{5} + i64q{7}).get() == 12);
}

// formatter coverage: wide character contexts and the ability enum

TEST_CASE("nexenne::utility::strong_typedef formats in a wide context") {
  CHECK(std::format(L"{}", chip_id{42}) == L"42");
  CHECK(std::format(L"{:>4}", chip_id{7}) == L"   7");
}

TEST_CASE("nexenne::utility::ability names and formats each flag") {
  CHECK(util::to_string(ability::none) == "none");
  CHECK(util::to_string(ability::add) == "add");
  CHECK(util::to_string(ability::scale) == "scale");
  CHECK(util::to_string(ability::shift) == "shift");
  CHECK(util::to_string(ability::arithmetic) == "arithmetic");
  CHECK(util::to_string(ability::comparable) == "comparable");
  CHECK(util::to_string(ability::bitwise) == "bitwise");
  // An ad-hoc union with no dedicated name reports unknown.
  CHECK(util::to_string(ability::add | ability::scale) == "unknown");

  // The formatter forwards to to_string and honours a string spec.
  CHECK(std::format("{}", ability::ratio) == "ratio");
  CHECK(std::format("{:>10}", ability::add) == "       add");

  // The new operator^ toggles a flag.
  CHECK((ability::arithmetic ^ ability::scale) == (ability::arithmetic & ~ability::scale));
  CHECK((ability::add ^ ability::add) == ability::none);
}

// is_strong_typedef is a usable public trait

TEST_CASE("nexenne::utility::is_strong_typedef reports wrapper types") {
  static_assert(util::is_strong_typedef<meters>::value);
  static_assert(util::is_strong_typedef<chip_id>::value);
  static_assert(!util::is_strong_typedef<double>::value);
  static_assert(!util::is_strong_typedef<int>::value);
  CHECK(true);
}

}  // namespace
