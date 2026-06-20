#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include <nexenne/math/fixed.hpp>

namespace math = nexenne::math;

namespace {
template <class S, std::size_t F>
concept fixed_instantiable = requires { typename math::fixed<S, F>; };
}  // namespace

// A pure-fractional format (integer_bits == 0) is rejected: its scale factor
// would not fit the signed storage. One integer bit is the minimum.
static_assert(fixed_instantiable<std::int16_t, 14>);
static_assert(!fixed_instantiable<std::int16_t, 15>);
static_assert(!fixed_instantiable<std::int32_t, 31>);

TEST_CASE("construction and conversion round-trip") {
  static_assert(math::q16_16::fraction_bits == 16);
  static_assert(math::q16_16::scale == (1 << 16));
  static_assert(std::is_same_v<math::q16_16::storage_type, std::int32_t>);

  constexpr math::q16_16 a{3};  // integer
  static_assert(a.to_int() == 3);
  static_assert(a.to_float() == 3.0);

  constexpr math::q16_16 b{1.5};  // float, exact in Q16.16
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
  // -0.5 in Q16.16 floors to -1, not 0 (documented behaviour).
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

  // Range endpoints.
  static_assert(math::q8_8::min().to_float() == -128.0);
  CHECK(math::q8_8::max().to_float() == doctest::Approx(127.99609375));
  static_assert(math::q16_16::min().to_float() == -32768.0);
  CHECK(math::q16_16::max().to_float() == doctest::Approx(32768.0).epsilon(1e-4));

  // min is one resolution below the negation of max (two's complement).
  static_assert(math::q16_16::min() < math::q16_16::max());
}

TEST_CASE("negating min() is defined, not UB (regression)") {
  // -INT_MIN has no representable positive; the result is min() itself, but the
  // operation must be well-defined (no signed-overflow UB). Verified clean under
  // UBSan; here we just pin the defined value.
  static_assert(-math::q16_16::min() == math::q16_16::min());
  static_assert(-math::q32_32::min() == math::q32_32::min());
}

TEST_CASE("multiply and divide round to nearest, symmetrically (regression)") {
  using q = math::q16_16;
  // Round to nearest, not truncate toward zero / floor: a half-ulp discarded
  // fraction rounds the magnitude up.
  CHECK((q::from_raw(1) * q::from_raw(0x8000)).raw() == 1);  // 0.5 ulp -> 1, not 0
  CHECK((q::from_raw(2) / q::from_raw(3)).raw() == 43691);   // 0.667 -> up, not 43690
  // Ties away from zero make the rounding symmetric, so the negative algebraic
  // identities hold bit-exactly (the old floor/truncate split broke them).
  CHECK((-q::from_raw(1) * q::from_raw(0x8000)).raw() == -1);
  CHECK((-q::from_raw(2) / q::from_raw(3)).raw() == -43691);
  CHECK((-q{0.1} * q{0.3}).raw() == -((q{0.1} * q{0.3}).raw()));
  CHECK((-q{0.1} / q{0.3}).raw() == -((q{0.1} / q{0.3}).raw()));
}
