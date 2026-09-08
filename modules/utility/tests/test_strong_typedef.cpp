/**
 * @file
 * @brief Tests for nexenne::utility::strong_typedef.
 */

#include <doctest/doctest.h>

#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <nexenne/utility/format.hpp>
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

struct id_tag;
using id16 = util::strong_typedef<id_tag, std::uint16_t, ability::comparable>;
using id32 = util::strong_typedef<id_tag, std::uint32_t, ability::comparable>;

using name = util::strong_typedef<struct name_tag, std::string, ability::comparable>;

using opaque = util::strong_typedef<struct opaque_tag, int>;

using length = util::quantity<struct length_tag, double>;
using token = util::identifier<struct token_tag, std::uint32_t>;
using seq = util::counter<struct seq_tag, int>;
using mask = util::bitfield<struct mask_tag, std::uint16_t>;

using bit = util::strong_typedef<struct bit_tag, bool, ability::boolean>;

static_assert(std::is_trivially_copyable_v<meters>);
static_assert(std::is_trivially_copyable_v<chip_id>);
static_assert(!std::is_trivially_copyable_v<name>);

static_assert(!util::same_tag_as<meters, chip_id>);
static_assert(util::same_tag_as<chip_id, chip_id>);
static_assert(util::same_tag_as<id16, id32>);
static_assert(util::strong_typedef_like<meters>);
static_assert(util::strong_typedef_like<meters const&>);
static_assert(!util::strong_typedef_like<double>);
static_assert(!util::strong_typedef_like<int>);

using alpha = util::strong_typedef<struct alpha_tag, int, ability::arithmetic>;
using beta = util::strong_typedef<struct beta_tag, int, ability::arithmetic>;
static_assert(!std::is_same_v<alpha, beta>);
static_assert(!std::is_convertible_v<alpha, beta>);
static_assert(!std::is_convertible_v<beta, alpha>);
static_assert(!std::is_convertible_v<alpha, int>);
static_assert(!std::is_convertible_v<int, alpha>);
static_assert(!std::is_convertible_v<int, opaque>);
static_assert(!util::same_tag_as<alpha, beta>);

static_assert(std::is_constructible_v<alpha, int>);
static_assert(std::is_constructible_v<int, alpha>);

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
  CHECK((meters{10.0} / meters{2.0}) == doctest::Approx(5.0));
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
  CHECK((c--).get() == 4);
  CHECK(c.get() == 3);
  CHECK((c++).get() == 3);
  CHECK(c.get() == 4);

  CHECK((+ticks{7}).get() == 7);
  CHECK(util::abs(ticks{-4}).get() == 4);
  CHECK(util::abs(counts{5}).get() == 5);
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

  CHECK((reg{1} << 7).get() == 0x80);
  CHECK(util::rotr(reg{0b0000'0011}, 1).get() == 0b1000'0001);
  CHECK(util::countl_zero(reg{0b0000'0001}) == 7);
  CHECK(util::countl_one(reg{0b1111'1111}) == 8);
  CHECK(util::countr_zero(reg{0b0000'1000}) == 3);
  CHECK(util::countr_one(reg{0b0000'0111}) == 3);
}

TEST_CASE("nexenne::utility::strong_typedef cross-underlying conversion, mixed ops, swap") {
  id32 const wide{id16{42}};
  CHECK(wide.get() == 42);
  CHECK(id16{1} == id32{1});

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
  CHECK(std::format("{:>4}", chip_id{42}) == "  42");

  auto id{chip_id{7}};
  util::to_underlying(id) = 9;
  CHECK(id.get() == 9);
}

TEST_CASE("nexenne::utility::ability bitmask union, intersection, complement, has") {
  constexpr auto both{ability::add | ability::subtract};
  CHECK(util::has(both, ability::add));
  CHECK(util::has(both, ability::subtract));
  CHECK_FALSE(util::has(both, ability::scale));

  CHECK(util::has(ability::arithmetic, ability::add));
  CHECK(util::has(ability::arithmetic, ability::scale));
  CHECK_FALSE(util::has(ability::add, ability::arithmetic));
  CHECK(util::has(ability::comparable, ability::equality | ability::ordered));

  constexpr auto inter{(ability::add | ability::scale) & (ability::scale | ability::ratio)};
  CHECK(inter == ability::scale);

  CHECK((ability::add | ability::none) == ability::add);
  CHECK((ability::add & ability::none) == ability::none);

  constexpr auto cleared{ability::arithmetic & ~ability::scale};
  CHECK_FALSE(util::has(cleared, ability::scale));
  CHECK(util::has(cleared, ability::add));
}

TEST_CASE("nexenne::utility::sanitized drops unsigned-only flags off non-unsigned T") {
  constexpr auto wanted{ability::shift | ability::bitops | ability::add};

  CHECK(util::sanitized<std::uint32_t>(wanted) == wanted);

  constexpr auto on_signed{util::sanitized<int>(wanted)};
  CHECK_FALSE(util::has(on_signed, ability::shift));
  CHECK_FALSE(util::has(on_signed, ability::bitops));
  CHECK(util::has(on_signed, ability::add));

  constexpr auto on_double{util::sanitized<double>(wanted)};
  CHECK_FALSE(util::has(on_double, ability::shift));
  CHECK(util::has(on_double, ability::add));
}

TEST_CASE("nexenne::utility::strong_typedef ready-made profiles expose the right abilities") {
  CHECK((length{4.0} + length{2.0}).get() == doctest::Approx(6.0));
  CHECK((length{4.0} * 3.0).get() == doctest::Approx(12.0));
  CHECK(length{1.0} < length{2.0});

  CHECK(token{1} == token{1});
  CHECK(token{1} != token{2});
  static_assert(!util::detail::has_op<token, ability::add>);

  auto s{seq{0}};
  ++s;
  ++s;
  CHECK(s.get() == 2);
  --s;
  CHECK(s.get() == 1);
  CHECK(seq{1} == s);
  static_assert(!util::detail::has_op<seq, ability::add>);
  static_assert(util::detail::has_op<seq, ability::increment>);

  CHECK((mask{0xF0} | mask{0x0F}).get() == 0xFF);
  CHECK((mask{0x01} << 4).get() == 0x10);
  CHECK(util::popcount(mask{0xFFFF}) == 16);
  CHECK(mask{1} != mask{2});
}

TEST_CASE("nexenne::utility::strong_typedef over std::string: compare, move, hash, format") {
  name const a{std::string{"alice"}};
  name const b{std::string{"bob"}};
  CHECK(a == a);
  CHECK(a != b);
  CHECK(a < b);
  CHECK(b > a);

  std::string source{"carol"};
  name moved{std::move(source)};
  CHECK(moved.get() == "carol");

  CHECK(std::hash<name>{}(a) == std::hash<std::string>{}(std::string{"alice"}));

  CHECK(std::format("{}", a) == "alice");
  CHECK(std::format("{:>7}", a) == "  alice");

  name editable{std::string{"x"}};
  editable.get() += "yz";
  CHECK(editable.get() == "xyz");
}

TEST_CASE("nexenne::utility::strong_typedef absent capabilities are removed from overloads") {
  static_assert(!util::detail::has_op<chip_id, ability::add>);
  static_assert(!util::detail::has_op<chip_id, ability::scale>);
  static_assert(!util::detail::has_op<chip_id, ability::increment>);
  static_assert(!util::detail::has_op<chip_id, ability::bit_and>);
  static_assert(util::detail::has_op<chip_id, ability::equality>);
  static_assert(util::detail::has_op<chip_id, ability::ordered>);

  static_assert(!util::detail::has_op<opaque, ability::equality>);
  static_assert(!util::detail::has_op<opaque, ability::ordered>);
  static_assert(!util::detail::has_op<opaque, ability::add>);

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
  CHECK_FALSE(has_and(meters{0.0}));
}

TEST_CASE("nexenne::utility::strong_typedef default construction value-initialises") {
  CHECK(meters{}.get() == doctest::Approx(0.0));
  CHECK(chip_id{}.get() == 0);
  CHECK(opaque{}.get() == 0);
  CHECK(name{}.get().empty());

  static_assert(std::is_default_constructible_v<meters>);
  static_assert(std::is_nothrow_default_constructible_v<chip_id>);
}

TEST_CASE("nexenne::utility::strong_typedef ordering yields the full relational set") {
  CHECK(meters{1.0} < meters{2.0});
  CHECK(meters{2.0} > meters{1.0});
  CHECK(meters{1.0} <= meters{1.0});
  CHECK(meters{1.0} >= meters{1.0});
  CHECK_FALSE(meters{2.0} < meters{1.0});

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

TEST_CASE("nexenne::utility::strong_typedef same-tag arithmetic keeps the strong type") {
  static_assert(std::is_same_v<decltype(meters{1.0} + meters{2.0}), meters>);
  static_assert(std::is_same_v<decltype(meters{1.0} - meters{2.0}), meters>);
  static_assert(std::is_same_v<decltype(-meters{1.0}), meters>);
  static_assert(std::is_same_v<decltype(meters{1.0} * 2.0), meters>);
  static_assert(std::is_same_v<decltype(2.0 * meters{1.0}), meters>);
  static_assert(std::is_same_v<decltype(meters{1.0} / 2.0), meters>);

  static_assert(std::is_same_v<decltype(meters{4.0} / meters{2.0}), double>);
  static_assert(std::is_same_v<decltype(ticks{4} / ticks{2}), int>);
  CHECK((ticks{9} / ticks{2}) == 4);
}

TEST_CASE("nexenne::utility::strong_typedef scale folds back into the underlying") {
  CHECK((ticks{7} * 2).get() == 14);
  CHECK((ticks{7} / 2).get() == 3);
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
  static_assert(std::is_same_v<decltype(sum)::value_type, std::uint32_t>);
  CHECK(sum.get() == 80000U);
}

struct sign_tag {};

using sign_ops =
  std::integral_constant<ability, ability::equality | ability::ordered | ability::add>;
using wide_signed = util::strong_typedef<sign_tag, std::int64_t, sign_ops::value>;
using signed_word = util::strong_typedef<sign_tag, std::int32_t, sign_ops::value>;
using narrow_signed = util::strong_typedef<sign_tag, std::int16_t, sign_ops::value>;
using unsigned_word = util::strong_typedef<sign_tag, std::uint32_t, sign_ops::value>;
using narrow_unsigned = util::strong_typedef<sign_tag, std::uint8_t, sign_ops::value>;

template <typename A, typename B>
concept addable_with = requires(A const& a, B const& b) { a + b; };

template <typename A, typename B>
concept equatable_with = requires(A const& a, B const& b) { a == b; };

static_assert(!addable_with<signed_word, unsigned_word>);
static_assert(!addable_with<unsigned_word, signed_word>);
static_assert(!equatable_with<signed_word, unsigned_word>);
static_assert(addable_with<narrow_signed, narrow_unsigned>);
static_assert(equatable_with<wide_signed, unsigned_word>);
static_assert(addable_with<unsigned_word, unsigned_word>);

TEST_CASE("nexenne::utility::strong_typedef mixes signedness only through a lossless common type") {
  CHECK((narrow_signed{-1} + narrow_unsigned{255}).get() == 254);
  CHECK(wide_signed{-1} != unsigned_word{4294967295U});
  CHECK(wide_signed{4294967295} == unsigned_word{4294967295U});
}

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

TEST_CASE("nexenne::utility::strong_typedef shift boundary counts and bit-counting edges") {
  CHECK((reg{0xAB} << 0).get() == 0xAB);
  CHECK((reg{0xAB} >> 0).get() == 0xAB);
  CHECK((reg{1} << 7).get() == 0x80);
  CHECK((reg{0x80} >> 7).get() == 1);

  CHECK(util::bit_width(reg{0}) == 0);
  CHECK(util::popcount(reg{0xFF}) == 8);
  CHECK(util::popcount(reg{0}) == 0);

  CHECK(util::countl_zero(reg{0}) == 8);
  CHECK(util::countr_zero(reg{0}) == 8);
  CHECK(util::countl_one(reg{0}) == 0);
  CHECK(util::countr_one(reg{0xFF}) == 8);

  CHECK(util::rotl(reg{0b1011'0010}, 8).get() == 0b1011'0010);
  CHECK(util::rotr(reg{0b1011'0010}, 8).get() == 0b1011'0010);
  CHECK(util::rotr(util::rotl(reg{0b0110'1001}, 3), 3).get() == 0b0110'1001);
}

TEST_CASE("nexenne::utility::strong_typedef saturating in-range and signed underflow/overflow") {
  CHECK(util::sat_add(sat{100}, sat{50}).get() == 150);
  CHECK(util::sat_sub(sat{100}, sat{50}).get() == 50);

  constexpr auto imax{std::numeric_limits<int>::max()};
  constexpr auto imin{std::numeric_limits<int>::min()};
  CHECK(util::sat_sub(sat_i{imax}, sat_i{-1}).get() == imax);
  CHECK(util::sat_add(sat_i{imin}, sat_i{-1}).get() == imin);
  CHECK(util::sat_add(sat_i{5}, sat_i{-3}).get() == 2);

  constexpr auto clamped{util::sat_add(sat{255}, sat{255})};
  static_assert(clamped.get() == 255);
}

TEST_CASE("nexenne::utility::strong_typedef boolean conversion semantics") {
  CHECK(static_cast<bool>(flag{5}));
  CHECK_FALSE(static_cast<bool>(flag{0}));
  static_assert(!std::is_convertible_v<flag, bool>);
  static_assert(std::is_constructible_v<bool, flag>);

  static_assert(std::is_constructible_v<bool, bit>);
  static_assert(!std::is_convertible_v<bit, bool>);
  CHECK(static_cast<bool>(bit{true}));
  CHECK_FALSE(static_cast<bool>(bit{false}));

  CHECK_FALSE(static_cast<bool>(flag{0}));
  CHECK(static_cast<bool>(flag{-1}));
}

TEST_CASE("nexenne::utility::strong_typedef explicit conversion to underlying") {
  meters const m{12.5};
  auto const raw{static_cast<double>(m)};
  CHECK(raw == doctest::Approx(12.5));

  static_assert(std::is_same_v<decltype(util::to_underlying(std::declval<meters&>())), double&>);
  static_assert(
    std::is_same_v<decltype(util::to_underlying(std::declval<meters const&>())), double const&>
  );
}

TEST_CASE("nexenne::utility::strong_typedef keys an unordered_map") {
  std::unordered_map<chip_id, std::string> by_id;
  by_id[chip_id{1}] = "one";
  by_id[chip_id{2}] = "two";
  CHECK(by_id.at(chip_id{1}) == "one");
  CHECK(by_id.at(chip_id{2}) == "two");
  CHECK(by_id.size() == 2);

  static_assert(noexcept(std::hash<chip_id>{}(chip_id{0})));
}

TEST_CASE("nexenne::utility::strong_typedef common_type unions underlying and abilities") {
  using common = std::common_type_t<id16, id32>;
  static_assert(std::is_same_v<common::value_type, std::uint32_t>);
  static_assert(std::is_same_v<common::tag_type, id_tag>);
  static_assert(util::has(common::abilities, ability::equality));
  static_assert(util::has(common::abilities, ability::ordered));
  CHECK(true);
}

struct mismatch_tag {};

using mismatch_number = util::strong_typedef<mismatch_tag, int, ability::equality>;
using mismatch_text = util::strong_typedef<mismatch_tag, std::string, ability::equality>;

template <typename A, typename B>
concept has_common_type = requires { typename std::common_type<A, B>::type; };

static_assert(!has_common_type<mismatch_number, mismatch_text>);
static_assert(!std::common_with<mismatch_number, mismatch_text>);
static_assert(has_common_type<id16, id32>);

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

  constexpr auto swapped{[] {
    auto a{ticks{1}};
    auto b{ticks{2}};
    using std::swap;
    swap(a, b);
    return a.get();
  }()};
  static_assert(swapped == 2);
}

using strval =
  util::strong_typedef<struct strval_tag, std::string, ability::add | ability::comparable>;

TEST_CASE("nexenne::utility::strong_typedef over a throwing underlying is conditionally noexcept") {
  static_assert(!noexcept(std::declval<strval&>() += std::declval<strval const&>()));
  static_assert(noexcept(std::declval<meters&>() += std::declval<meters const&>()));

  strval a{std::string{"foo"}};
  a += strval{std::string{"bar"}};
  CHECK(a.get() == "foobar");

  auto const joined{strval{std::string{"ab"}} + strval{std::string{"cd"}}};
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(joined)>, strval>);
  CHECK(joined.get() == "abcd");
  CHECK(strval{std::string{"x"}} != strval{std::string{"y"}});
}

TEST_CASE("nexenne::utility::strong_typedef rotate accepts negative counts") {
  CHECK(util::rotl(reg{0b0110'1001}, -1).get() == util::rotr(reg{0b0110'1001}, 1).get());
  CHECK(util::rotr(reg{0b0110'1001}, -1).get() == util::rotl(reg{0b0110'1001}, 1).get());
}

struct conv_tag;
using conv_lean = util::strong_typedef<conv_tag, int, ability::comparable>;
using conv_rich = util::strong_typedef<conv_tag, int, ability::arithmetic | ability::comparable>;

TEST_CASE("nexenne::utility::strong_typedef same-tag conversions narrow and reshape abilities") {
  id16 const narrow{id32{70000}};
  CHECK(narrow.get() == static_cast<std::uint16_t>(70000));

  conv_rich const rich{42};
  conv_lean const lean{rich};
  CHECK(lean.get() == 42);
  static_assert(std::is_constructible_v<conv_lean, conv_rich>);
  static_assert(std::is_constructible_v<conv_rich, conv_lean>);
  static_assert(!std::is_convertible_v<conv_rich, conv_lean>);
}

using eq_only = util::strong_typedef<struct eq_only_tag, int, ability::equality>;
using ord_only = util::strong_typedef<struct ord_only_tag, int, ability::ordered>;

TEST_CASE("nexenne::utility::strong_typedef equality and ordering are independent capabilities") {
  auto const has_eq{[]<typename U>(U) { return requires(U x) { x == x; }; }};
  auto const has_lt{[]<typename U>(U) { return requires(U x) { x < x; }; }};

  CHECK(has_eq(eq_only{0}));
  CHECK_FALSE(has_lt(eq_only{0}));

  CHECK(has_lt(ord_only{0}));
  CHECK_FALSE(has_eq(ord_only{0}));

  CHECK(eq_only{1} == eq_only{1});
  CHECK(ord_only{1} < ord_only{2});
}

TEST_CASE("nexenne::utility::strong_typedef abs clears the sign of negative zero") {
  auto const zero{util::abs(meters{-0.0})};
  CHECK(std::signbit(zero.get()) == false);
  CHECK(zero.get() == doctest::Approx(0.0));
  CHECK(util::abs(meters{-2.5}).get() == doctest::Approx(2.5));
}

TEST_CASE(
  "nexenne::utility::strong_typedef get on an rvalue moves a heap-allocated underlying out"
) {
  static_assert(std::is_same_v<decltype(std::declval<name>().get()), std::string&&>);
  static_assert(std::is_same_v<decltype(std::declval<name const>().get()), std::string const&&>);

  name src{std::string(64, 'x')};
  std::string const moved{std::move(src).get()};
  CHECK(moved.size() == 64);
  CHECK(src.get().empty());  // libstdc++ leaves a moved-from string empty
}

TEST_CASE("nexenne::utility::strong_typedef saturating promotes mixed underlyings") {
  struct smix_tag;
  using s16 = util::strong_typedef<smix_tag, std::uint16_t, ability::saturating>;
  using s32 = util::strong_typedef<smix_tag, std::uint32_t, ability::saturating>;

  auto const summed{util::sat_add(s16{60000}, s32{10000})};
  static_assert(std::is_same_v<decltype(summed)::value_type, std::uint32_t>);
  CHECK(summed.get() == 70000U);

  auto const diff{util::sat_sub(s16{5}, s32{9})};
  static_assert(std::is_same_v<decltype(diff)::value_type, std::uint32_t>);
  CHECK(diff.get() == 0U);
}

TEST_CASE("nexenne::utility::strong_typedef sanitized drops unsigned-only ops on a mixed result") {
  struct mix_tag;
  using u32reg =
    util::strong_typedef<mix_tag, std::uint32_t, ability::add | ability::shift | ability::bitops>;
  using i64q = util::strong_typedef<mix_tag, std::int64_t, ability::add>;

  using sum_t = decltype(u32reg{5} + i64q{7});
  static_assert(std::is_same_v<sum_t::value_type, std::int64_t>);
  static_assert(util::detail::has_op<sum_t, ability::add>);
  static_assert(!util::detail::has_op<sum_t, ability::shift>);
  static_assert(!util::detail::has_op<sum_t, ability::bitops>);
  CHECK((u32reg{5} + i64q{7}).get() == 12);
}

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
  CHECK(util::to_string(ability::add | ability::scale) == "unknown");

  CHECK(std::format("{}", ability::ratio) == "ratio");
  CHECK(std::format("{:>10}", ability::add) == "       add");

  CHECK((ability::arithmetic ^ ability::scale) == (ability::arithmetic & ~ability::scale));
  CHECK((ability::add ^ ability::add) == ability::none);
}

TEST_CASE("nexenne::utility::is_strong_typedef reports wrapper types") {
  static_assert(util::is_strong_typedef<meters>::value);
  static_assert(util::is_strong_typedef<chip_id>::value);
  static_assert(!util::is_strong_typedef<double>::value);
  static_assert(!util::is_strong_typedef<int>::value);
  CHECK(true);
}

}  // namespace
