/**
 * @file
 * @brief Tests for nexenne::random uniform draws.
 */

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <nexenne/random/pcg.hpp>
#include <nexenne/random/uniform.hpp>
#include <nexenne/random/xoshiro.hpp>

namespace {

namespace rnd = nexenne::random;

TEST_CASE("nexenne::random::uniform_int stays in the closed range and covers it") {
  rnd::pcg32 g{7, 1};
  std::array<int, 6> hits{};
  for (int i{0}; i < 60000; ++i) {
    auto const v{rnd::uniform_int(g, 1, 6)};  // a die
    REQUIRE(v >= 1);
    REQUIRE(v <= 6);
    ++hits[static_cast<std::size_t>(v - 1)];
  }
  for (auto const h : hits) {
    CHECK(h > 8000);  // every face appears, roughly 10000 each
  }
}

TEST_CASE("nexenne::random::uniform_int handles a single-value and a wide range") {
  rnd::xoshiro256ss g{1};
  CHECK(rnd::uniform_int(g, 5, 5) == 5);  // lo == hi
  for (int i{0}; i < 1000; ++i) {
    auto const v{rnd::uniform_int<std::int64_t>(g, -1'000'000'000'000LL, 1'000'000'000'000LL)};
    CHECK(v >= -1'000'000'000'000LL);
    CHECK(v <= 1'000'000'000'000LL);
  }
}

TEST_CASE("nexenne::random::uniform_int full 64-bit range fills the high word with a 32-bit engine"
) {
  // Regression: the range == 0 (full-range) path must draw enough engine words
  // to cover the whole Int. pcg32 yields 32 bits per call, so a 64-bit full
  // range needs two draws; a single draw would leave the high 32 bits zero.
  rnd::pcg32 g{123, 1};
  std::uint64_t seen_bits{0};
  bool saw_negative{false};
  bool saw_large_positive{false};
  for (int i{0}; i < 5000; ++i) {
    auto const v{rnd::uniform_int<std::int64_t>(
      g, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()
    )};
    seen_bits |= static_cast<std::uint64_t>(v);
    if (v < std::numeric_limits<std::int32_t>::min()) {
      saw_negative = true;
    }
    if (v > std::int64_t{1} << 40) {
      saw_large_positive = true;
    }
  }
  CHECK((seen_bits >> 32) != 0u);  // high 32 bits are exercised, not stuck at zero
  CHECK(saw_negative);             // values beyond the low-32 window appear
  CHECK(saw_large_positive);
}

TEST_CASE("nexenne::random::uniform_real lies in [0, 1) and averages near 0.5") {
  rnd::pcg32 g{3, 1};
  double sum{0.0};
  constexpr int n{100000};
  for (int i{0}; i < n; ++i) {
    auto const u{rnd::uniform_real(g)};
    REQUIRE(u >= 0.0);
    REQUIRE(u < 1.0);
    sum += u;
  }
  CHECK(sum / n == doctest::Approx(0.5).epsilon(0.02));
}

TEST_CASE("nexenne::random::bernoulli is fair, and the weighted form clamps") {
  rnd::pcg32 g{9, 1};
  int trues{0};
  constexpr int n{100000};
  for (int i{0}; i < n; ++i) {
    if (rnd::bernoulli(g)) {
      ++trues;
    }
  }
  CHECK(static_cast<double>(trues) / n == doctest::Approx(0.5).epsilon(0.02));

  CHECK_FALSE(rnd::bernoulli(g, 0.0));  // p <= 0 always false
  CHECK_FALSE(rnd::bernoulli(g, -1.0));
  CHECK(rnd::bernoulli(g, 1.0));  // p >= 1 always true
  CHECK(rnd::bernoulli(g, 2.0));

  int hits{0};
  for (int i{0}; i < n; ++i) {
    if (rnd::bernoulli(g, 0.25)) {
      ++hits;
    }
  }
  CHECK(static_cast<double>(hits) / n == doctest::Approx(0.25).epsilon(0.02));
}

// unbiasedness: the headline property

TEST_CASE("nexenne::random::uniform_int is unbiased over a non-power-of-two range (chi-square)") {
  // [0, 6] has width 7. A naive `engine % 7` would be biased because 2^32 is
  // not a multiple of 7; Lemire's rejection sampler must remove that bias.
  // Goodness-of-fit: with 7 buckets there are 6 degrees of freedom; the
  // chi-square 0.999 critical value is ~22.46, so a fair sampler comfortably
  // clears a generous threshold of 30 (a biased `% 7` would blow far past it).
  rnd::pcg32 g{20240619u, 42u};
  constexpr int buckets{7};
  constexpr long n{7'000'000};
  std::array<long, buckets> hits{};
  for (long i{0}; i < n; ++i) {
    auto const v{rnd::uniform_int(g, 0, 6)};
    REQUIRE(v >= 0);
    REQUIRE(v <= 6);
    ++hits[static_cast<std::size_t>(v)];
  }
  auto const expected{static_cast<double>(n) / buckets};
  double chi_square{0.0};
  for (auto const h : hits) {
    auto const d{static_cast<double>(h) - expected};
    chi_square += (d * d) / expected;
  }
  CHECK(chi_square < 30.0);

  // Per-bucket band as a second, independent check: each count is within
  // ~0.5% of the mean. A `% 7` bias would push the under-represented buckets
  // outside this band, so this both detects bias and pins the magnitude.
  for (auto const h : hits) {
    CHECK(static_cast<double>(h) == doctest::Approx(expected).epsilon(0.005));
  }
}

TEST_CASE("nexenne::random::uniform_int is unbiased on the wide (>2^32) modulo-rejection path") {
  // A 64-bit-output engine plus a range above 2^32 takes the wide path, which
  // uses modulo-with-rejection rather than Lemire. Use 5 non-power-of-two
  // buckets carved out of a wide range and check the distribution is flat.
  rnd::xoshiro256ss g{0xfeed'beefULL};
  constexpr std::int64_t lo{-3'000'000'000LL};
  constexpr std::int64_t hi{6'999'999'999LL};  // width 10'000'000'000 > 2^32
  constexpr int buckets{5};
  constexpr std::int64_t span{(hi - lo + 1) / buckets};  // 2'000'000'000 each
  constexpr long n{5'000'000};
  std::array<long, buckets> hits{};
  for (long i{0}; i < n; ++i) {
    auto const v{rnd::uniform_int<std::int64_t>(g, lo, hi)};
    REQUIRE(v >= lo);
    REQUIRE(v <= hi);
    auto const b{static_cast<int>((v - lo) / span)};
    ++hits[static_cast<std::size_t>(b)];
  }
  auto const expected{static_cast<double>(n) / buckets};
  double chi_square{0.0};
  for (auto const h : hits) {
    auto const d{static_cast<double>(h) - expected};
    chi_square += (d * d) / expected;
  }
  CHECK(chi_square < 25.0);  // 4 d.o.f., 0.999 critical ~18.5
}

// integer endpoints, single values, and signed ranges

TEST_CASE("nexenne::random::uniform_int hits both endpoints of a small range") {
  // Width 5 over enough draws must produce both the minimum and the maximum,
  // not just interior values. P(missing an endpoint) is astronomically small.
  rnd::pcg32 g{555u, 3u};
  bool saw_lo{false};
  bool saw_hi{false};
  for (int i{0}; i < 100000; ++i) {
    auto const v{rnd::uniform_int(g, 10, 14)};
    REQUIRE(v >= 10);
    REQUIRE(v <= 14);
    if (v == 10) {
      saw_lo = true;
    }
    if (v == 14) {
      saw_hi = true;
    }
  }
  CHECK(saw_lo);
  CHECK(saw_hi);
}

TEST_CASE("nexenne::random::uniform_int single-value ranges always return that value") {
  rnd::pcg32 g{1u, 1u};
  rnd::xoshiro256ss h{2};
  for (int i{0}; i < 1000; ++i) {
    CHECK(rnd::uniform_int(g, 0, 0) == 0);     // [0, 0]
    CHECK(rnd::uniform_int(g, 7, 7) == 7);     // [k, k] positive
    CHECK(rnd::uniform_int(g, -9, -9) == -9);  // [k, k] negative
    CHECK(rnd::uniform_int<std::int64_t>(h, -42, -42) == -42);
    CHECK(
      rnd::uniform_int<std::int64_t>(
        h, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min()
      )
      == std::numeric_limits<std::int64_t>::min()
    );
  }
}

TEST_CASE("nexenne::random::uniform_int handles wholly negative ranges") {
  rnd::pcg32 g{77u, 5u};
  bool saw_lo{false};
  bool saw_hi{false};
  for (int i{0}; i < 100000; ++i) {
    auto const v{rnd::uniform_int(g, -10, -4)};  // width 7, non-power-of-two
    REQUIRE(v >= -10);
    REQUIRE(v <= -4);
    if (v == -10) {
      saw_lo = true;
    }
    if (v == -4) {
      saw_hi = true;
    }
  }
  CHECK(saw_lo);
  CHECK(saw_hi);
}

TEST_CASE("nexenne::random::uniform_int covers the full 32-bit signed range") {
  // range == 0 (full-width) path for a 32-bit Int. Every value of int is
  // reachable; check both extremes of the sign and that the high bits move.
  rnd::xoshiro256ss g{0x1234'5678ULL};
  std::uint32_t seen_bits{0};
  bool saw_negative{false};
  bool saw_positive{false};
  for (int i{0}; i < 20000; ++i) {
    auto const v{rnd::uniform_int<std::int32_t>(
      g, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()
    )};
    seen_bits |= static_cast<std::uint32_t>(v);
    if (v < (std::numeric_limits<std::int32_t>::min() / 2)) {
      saw_negative = true;
    }
    if (v > (std::numeric_limits<std::int32_t>::max() / 2)) {
      saw_positive = true;
    }
  }
  CHECK((seen_bits & 0x8000'0000u) != 0u);  // sign bit exercised
  CHECK(saw_negative);
  CHECK(saw_positive);
}

TEST_CASE("nexenne::random::uniform_int covers the full unsigned 32-bit range") {
  // Full-width path with an unsigned Int: range underflows to 0 and the helper
  // must fill all 32 bits, including the top bit, never clamping the high half.
  rnd::pcg32 g{0xabcdu, 7u};
  std::uint32_t or_bits{0};
  std::uint32_t and_bits{0xffff'ffffu};
  for (int i{0}; i < 20000; ++i) {
    auto const v{rnd::uniform_int<std::uint32_t>(
      g, std::numeric_limits<std::uint32_t>::min(), std::numeric_limits<std::uint32_t>::max()
    )};
    or_bits |= v;
    and_bits &= v;
  }
  CHECK(or_bits == 0xffff'ffffu);  // every bit position seen set at least once
  CHECK(and_bits == 0u);           // every bit position seen clear at least once
}

// reproducibility

TEST_CASE("nexenne::random::uniform_int is reproducible from a fixed seed") {
  auto draw_sequence{[](auto make_engine) {
    auto g{make_engine()};
    std::array<int, 64> seq{};
    for (auto& s : seq) {
      s = rnd::uniform_int(g, -100, 100);
    }
    return seq;
  }};

  auto const a{draw_sequence([] { return rnd::pcg32{99u, 1u}; })};
  auto const b{draw_sequence([] { return rnd::pcg32{99u, 1u}; })};
  CHECK(a == b);  // same seed -> identical sequence

  auto const c{draw_sequence([] { return rnd::pcg32{100u, 1u}; })};
  CHECK(a != c);  // different seed -> different sequence (overwhelmingly likely)

  auto const x{draw_sequence([] { return rnd::xoshiro256ss{12345u}; })};
  auto const y{draw_sequence([] { return rnd::xoshiro256ss{12345u}; })};
  CHECK(x == y);
}

TEST_CASE("nexenne::random::uniform_real is reproducible from a fixed seed") {
  rnd::pcg32 a{2024u, 1u};
  rnd::pcg32 b{2024u, 1u};
  for (int i{0}; i < 1000; ++i) {
    CHECK(rnd::uniform_real(a) == rnd::uniform_real(b));
  }
}

// real / float distribution

TEST_CASE("nexenne::random::uniform_real never returns 1.0 and can approach both ends") {
  // The largest 53-bit mantissa gives (2^53 - 1) / 2^53 < 1, so 1.0 is
  // unreachable by construction. Verify across both engines, and check the
  // sampler reaches values close to 0 and close to 1 over many draws.
  rnd::pcg32 g{31u, 1u};
  rnd::xoshiro256ss h{0x5eedULL};
  double smallest{1.0};
  double largest{0.0};
  for (int i{0}; i < 500000; ++i) {
    auto const u{rnd::uniform_real(g)};
    auto const w{rnd::uniform_real(h)};
    REQUIRE(u >= 0.0);
    REQUIRE(u < 1.0);  // strictly below one
    REQUIRE(w >= 0.0);
    REQUIRE(w < 1.0);
    smallest = u < smallest ? u : smallest;
    largest = u > largest ? u : largest;
  }
  CHECK(smallest < 0.001);  // gets close to the bottom of the unit interval
  CHECK(largest > 0.999);   // gets close to (but never reaches) the top
}

TEST_CASE("nexenne::random::uniform_real is unbiased across the unit interval (chi-square)") {
  // Ten equal buckets across [0, 1). A correctly uniform sampler fills them
  // evenly; this catches a constant-bit truncation or a scale error that the
  // mean-only check would miss.
  rnd::xoshiro256ss g{0xc0ffeeULL};
  constexpr int buckets{10};
  constexpr long n{2'000'000};
  std::array<long, buckets> hits{};
  for (long i{0}; i < n; ++i) {
    auto const u{rnd::uniform_real(g)};
    REQUIRE(u >= 0.0);
    REQUIRE(u < 1.0);
    auto b{static_cast<int>(u * buckets)};
    if (b >= buckets) {
      b = buckets - 1;  // defensive; u < 1 keeps b in range already
    }
    ++hits[static_cast<std::size_t>(b)];
  }
  auto const expected{static_cast<double>(n) / buckets};
  double chi_square{0.0};
  for (auto const h : hits) {
    auto const d{static_cast<double>(h) - expected};
    chi_square += (d * d) / expected;
  }
  CHECK(chi_square < 35.0);  // 9 d.o.f., 0.999 critical ~27.9
}

// bernoulli edge probabilities

TEST_CASE("nexenne::random::bernoulli(p) at the boundary probabilities") {
  rnd::pcg32 g{8u, 1u};
  // Exactly 0.0 and 1.0 are short-circuited and consume no draws; NaN compares
  // false against every threshold, so it falls through to the sampler (never
  // forced true or false) -- exercise it to confirm it does not trap.
  CHECK_FALSE(rnd::bernoulli(g, 0.0));
  CHECK(rnd::bernoulli(g, 1.0));
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  static_cast<void>(rnd::bernoulli(g, nan));  // must not assert/trap

  // A probability just below 1 should almost always be true; just above 0
  // almost always false. Confirms the comparison direction is correct.
  int near_one{0};
  int near_zero{0};
  constexpr int n{100000};
  for (int i{0}; i < n; ++i) {
    if (rnd::bernoulli(g, 0.999)) {
      ++near_one;
    }
    if (rnd::bernoulli(g, 0.001)) {
      ++near_zero;
    }
  }
  CHECK(near_one > n - 500);  // ~99.9% true
  CHECK(near_zero < 500);     // ~0.1% true
}

}  // namespace
