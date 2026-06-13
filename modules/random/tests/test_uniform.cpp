/**
 * @file
 * @brief Tests for nexenne::random uniform draws.
 */

#include <doctest/doctest.h>

#include <array>
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

}  // namespace
