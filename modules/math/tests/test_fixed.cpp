#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <nexenne/math/fixed.hpp>

namespace math = nexenne::math;

namespace {
template <class S, std::size_t F>
concept fixed_instantiable = requires { typename math::fixed<S, F>; };
}  // namespace

// integer_bits == 0 is rejected: its scale factor would not fit the signed storage.
static_assert(fixed_instantiable<std::int16_t, 14>);
static_assert(!fixed_instantiable<std::int16_t, 15>);
static_assert(!fixed_instantiable<std::int32_t, 31>);

TEST_CASE("value_type aliases the storage type") {
  static_assert(std::is_same_v<math::q16_16::value_type, std::int32_t>);
  static_assert(std::is_same_v<math::q8_8::value_type, std::int16_t>);
  static_assert(std::is_same_v<math::q16_16::value_type, math::q16_16::storage_type>);
}

TEST_CASE("float construction truncates toward zero") {
  // 0.99999 is within half an ulp of 1.0 but truncates to raw 65535, not 65536.
  static_assert(math::q16_16{0.99999f}.raw() == 65535);
  static_assert(math::q16_16{-0.99999f}.raw() == -65535);
}

TEST_CASE("construction and conversion round-trip") {
  static_assert(math::q16_16::fraction_bits == 16);
  static_assert(math::q16_16::scale == (1 << 16));
  static_assert(std::is_same_v<math::q16_16::storage_type, std::int32_t>);

  constexpr math::q16_16 a{3};
  static_assert(a.to_int() == 3);
  static_assert(a.to_float() == 3.0);

  constexpr math::q16_16 b{1.5};
  static_assert(b.to_float() == 1.5);
  static_assert(b.raw() == (1 << 16) + (1 << 15));
}

TEST_CASE("arithmetic is exact on representable values") {
  constexpr math::q16_16 a{2.5};
  constexpr math::q16_16 b{0.5};
  static_assert((a + b).to_float() == 3.0);
  static_assert((a - b).to_float() == 2.0);
  static_assert((a * b).to_float() == 1.25);
  static_assert((a / b).to_float() == 5.0);
  static_assert((-a).to_float() == -2.5);
}

TEST_CASE("multiply uses a wider intermediate to avoid overflow") {
  // 100 * 100 = 10000 fits Q16.16 (max about +/-32768), but the raw mid-product
  // (100<<16)^2 = 4.3e13 overflows int32; the int64 widening keeps it exact.
  constexpr math::q16_16 a{100.0};
  constexpr math::q16_16 b{100.0};
  CHECK((a * b).to_float() == doctest::Approx(10000.0));

  // q32_32 exercises the __int128 wide path: (1000<<32)^2 overflows int64.
  math::q32_32 c{1000.0};
  math::q32_32 d{1000.0};
  CHECK((c * d).to_float() == doctest::Approx(1000000.0));
}

TEST_CASE("compound assignment and comparison") {
  math::q16_16 a{1.0};
  a += math::q16_16{0.5};
  CHECK(a.to_float() == doctest::Approx(1.5));
  a *= math::q16_16{2.0};
  CHECK(a.to_float() == doctest::Approx(3.0));

  static_assert(math::q16_16{1.0} < math::q16_16{2.0});
  static_assert(math::q16_16{2.0} == math::q16_16{2.0});
}

TEST_CASE("to_int arithmetic-shifts toward negative infinity") {
  constexpr math::q16_16 neg{-0.5};
  static_assert(neg.to_int() == -1);
}

TEST_CASE("representable range and resolution are queryable") {
  static_assert(math::q8_8::integer_bits == 7);
  static_assert(math::q16_16::integer_bits == 15);
  static_assert(math::q32_32::integer_bits == 31);

  // Resolution is the smallest positive step, 1/2^FractionBits.
  static_assert(math::q8_8::resolution().to_float() == 1.0 / 256.0);
  static_assert(math::q16_16::resolution().to_float() == 1.0 / 65536.0);

  static_assert(math::q8_8::min().to_float() == -128.0);
  CHECK(math::q8_8::max().to_float() == doctest::Approx(127.99609375));
  static_assert(math::q16_16::min().to_float() == -32768.0);
  CHECK(math::q16_16::max().to_float() == doctest::Approx(32768.0).epsilon(1e-4));

  static_assert(math::q16_16::min() < math::q16_16::max());
}

TEST_CASE("negating min() is defined, not UB") {
  // -INT_MIN has no representable positive, so the defined result is min() itself.
  static_assert(-math::q16_16::min() == math::q16_16::min());
  static_assert(-math::q32_32::min() == math::q32_32::min());
}

TEST_CASE("multiply and divide round to nearest, symmetrically") {
  using q = math::q16_16;
  CHECK((q::from_raw(1) * q::from_raw(0x8000)).raw() == 1);  // 0.5 ulp -> 1, not 0
  CHECK((q::from_raw(2) / q::from_raw(3)).raw() == 43691);   // 0.667 -> up, not 43690
  // Ties away from zero keep the negated identities bit-exact.
  CHECK((-q::from_raw(1) * q::from_raw(0x8000)).raw() == -1);
  CHECK((-q::from_raw(2) / q::from_raw(3)).raw() == -43691);
  CHECK((-q{0.1} * q{0.3}).raw() == -((q{0.1} * q{0.3}).raw()));
  CHECK((-q{0.1} / q{0.3}).raw() == -((q{0.1} / q{0.3}).raw()));
}

TEST_CASE("fixed raw() writes the representation on a mutable value") {
  using q16 = math::fixed<std::int32_t, 16>;
  auto x{q16{}};
  static_assert(std::is_same_v<decltype(x.raw()), std::int32_t&>);
  static_assert(std::is_same_v<decltype(std::as_const(x).raw()), std::int32_t>);
  x.raw() = q16::scale + q16::scale / 2;
  CHECK(x == q16::from_raw(q16::scale + q16::scale / 2));
  CHECK(x.to_float<float>() == 1.5F);
}
