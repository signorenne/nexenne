/**
 * @file
 * @brief Deep tests for the robust and adaptive filters
 * (median, kalman, complementary, lms).
 */

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <span>

#include <nexenne/filter/filter.hpp>

namespace {

namespace flt = nexenne::filter;

// median, kalman, and complementary satisfy the shared filter_like surface.
// complementary qualifies via its single-argument push overload (added for
// exactly this reason); lms does NOT, because its push takes two arguments
// (input and desired), so it is deliberately excluded from filter_like.
static_assert(flt::filter_like<flt::median<double, 3>>);
static_assert(flt::filter_like<flt::median<float, 3>>);
static_assert(flt::filter_like<flt::median<double, 4>>);
static_assert(flt::filter_like<flt::kalman<double>>);
static_assert(flt::filter_like<flt::kalman<float>>);
static_assert(flt::filter_like<flt::complementary<double>>);
static_assert(flt::filter_like<flt::complementary<float>>);
static_assert(!flt::filter_like<flt::lms<double, 4>>);
static_assert(!flt::filter_like<flt::lms<float, 4>>);

// Static surface checks: window_size / taps are compile-time constants.
static_assert(flt::median<double, 5>::window_size == 5);
static_assert(flt::median<double, 3>::window_size == 3);
static_assert(flt::lms<double, 7>::taps == 7);

TEST_CASE("nexenne::filter::median default-constructed state") {
  auto f{flt::median<double, 3>{}};
  CHECK_FALSE(f.filled());
  CHECK(f.value() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::median rejects a single spike while passing the level") {
  // Subsumes the legacy "rejects a single spike" case.
  auto f{flt::median<double, 3>{}};
  static_cast<void>(f.push(10.0));
  static_cast<void>(f.push(10.0));
  static_cast<void>(f.push(10.0));
  CHECK(f.filled());
  CHECK(f.push(1000.0) == doctest::Approx(10.0));  // spike rejected
  CHECK(f.push(10.0) == doctest::Approx(10.0));
  CHECK(f.value() == doctest::Approx(10.0));
}

TEST_CASE("nexenne::filter::median window-fill transient (N=4, exact values)") {
  // Each push the count grows; the lower-middle index is (count - 1) / 2.
  auto f{flt::median<double, 4>{}};
  CHECK_FALSE(f.filled());
  CHECK(f.push(5.0) == doctest::Approx(5.0));  // {5}                 idx 0
  CHECK_FALSE(f.filled());
  CHECK(f.push(1.0) == doctest::Approx(1.0));  // {1,5}    idx (2-1)/2=0 -> 1
  CHECK(f.push(9.0) == doctest::Approx(5.0));  // {1,5,9}  idx (3-1)/2=1 -> 5
  CHECK(f.push(3.0) == doctest::Approx(3.0));  // {1,3,5,9} idx (4-1)/2=1 -> 3
  CHECK(f.filled());
}

TEST_CASE("nexenne::filter::median EVEN window returns the LOWER-middle element") {
  // KNOWN-CORRECT audited behaviour: for an even, full window the lower of the
  // two central values is returned, i.e. index (N - 1) / 2 of the sorted set.
  auto f{flt::median<double, 4>{}};
  // Push an unsorted permutation of {1,2,3,4}; sorted -> {1,2,3,4}.
  static_cast<void>(f.push(4.0));
  static_cast<void>(f.push(2.0));
  static_cast<void>(f.push(1.0));
  auto const v{f.push(3.0)};  // index (4-1)/2 = 1 -> 2.0 (lower middle, not 3.0)
  CHECK(v == doctest::Approx(2.0));
  CHECK(f.value() == doctest::Approx(2.0));

  // A second even window with a different known sorted set {2,4,6,8} -> 4.0.
  auto g{flt::median<double, 4>{}};
  static_cast<void>(g.push(8.0));
  static_cast<void>(g.push(6.0));
  static_cast<void>(g.push(2.0));
  CHECK(g.push(4.0) == doctest::Approx(4.0));  // index 1 of {2,4,6,8}
}

TEST_CASE("nexenne::filter::median even window N=2 returns the lower of two") {
  auto f{flt::median<double, 2>{}};
  CHECK(f.push(7.0) == doctest::Approx(7.0));   // {7}
  CHECK(f.push(3.0) == doctest::Approx(3.0));   // buf {7,3} sorted {3,7} idx 0 -> 3 (lower)
  CHECK(f.push(10.0) == doctest::Approx(3.0));  // ring overwrites slot 0: buf {10,3}
                                                // sorted {3,10}, idx (2-1)/2 = 0 -> 3 (lower)
}

TEST_CASE("nexenne::filter::median window size 1 is passthrough") {
  auto f{flt::median<double, 1>{}};
  CHECK(f.push(3.0) == doctest::Approx(3.0));
  CHECK(f.filled());  // a single sample fills a window of one
  CHECK(f.push(-100.0) == doctest::Approx(-100.0));
  CHECK(f.push(42.5) == doctest::Approx(42.5));
  CHECK(f.value() == doctest::Approx(42.5));
}

TEST_CASE("nexenne::filter::median monotonic ramp through a full window") {
  auto f{flt::median<double, 3>{}};
  static_cast<void>(f.push(1.0));
  static_cast<void>(f.push(2.0));
  CHECK(f.push(3.0) == doctest::Approx(2.0));  // {1,2,3} -> 2
  CHECK(f.push(4.0) == doctest::Approx(3.0));  // {2,3,4} -> 3
  CHECK(f.push(5.0) == doctest::Approx(4.0));  // {3,4,5} -> 4
  CHECK(f.push(6.0) == doctest::Approx(5.0));  // {4,5,6} -> 5
}

TEST_CASE("nexenne::filter::median tracks a step after a majority of the window flips") {
  // Subsumes the legacy "tracks a step change after N/2+1 samples" case.
  auto f{flt::median<double, 5>{}};
  for (auto i{0}; i < 5; ++i) {
    static_cast<void>(f.push(0.0));
  }
  CHECK(f.value() == doctest::Approx(0.0));
  static_cast<void>(f.push(100.0));  // {0,0,0,0,100} -> 0
  CHECK(f.value() == doctest::Approx(0.0));
  static_cast<void>(f.push(100.0));  // {0,0,0,100,100} -> 0
  CHECK(f.value() == doctest::Approx(0.0));
  auto const v{f.push(100.0)};         // {0,0,100,100,100} -> 100
  CHECK(v == doctest::Approx(100.0));  // 3 of 5 are now 100
}

TEST_CASE("nexenne::filter::median repeated (all-equal) values") {
  auto f{flt::median<double, 5>{}};
  for (auto i{0}; i < 20; ++i) {
    CHECK(f.push(7.0) == doctest::Approx(7.0));
  }
  CHECK(f.value() == doctest::Approx(7.0));
}

TEST_CASE("nexenne::filter::median alternating extremes settle on a stable median") {
  auto f{flt::median<double, 3>{}};
  static_cast<void>(f.push(-1e6));
  static_cast<void>(f.push(1e6));
  // window {-1e6, 1e6, -1e6} sorted -> middle is -1e6
  CHECK(f.push(-1e6) == doctest::Approx(-1e6));
  // window {1e6, -1e6, 1e6} sorted -> middle is 1e6
  CHECK(f.push(1e6) == doctest::Approx(1e6));
}

TEST_CASE("nexenne::filter::median single non-zero sample among zeros") {
  auto f{flt::median<double, 5>{}};
  for (auto i{0}; i < 5; ++i) {
    static_cast<void>(f.push(0.0));
  }
  CHECK(f.push(999.0) == doctest::Approx(0.0));  // lone spike rejected
  CHECK(f.value() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::median reset clears the window and value") {
  auto f{flt::median<double, 3>{}};
  static_cast<void>(f.push(5.0));
  static_cast<void>(f.push(6.0));
  static_cast<void>(f.push(7.0));
  CHECK(f.filled());
  CHECK(f.value() == doctest::Approx(6.0));
  f.reset();
  CHECK_FALSE(f.filled());
  CHECK(f.value() == doctest::Approx(0.0));
  // After reset the count restarts: a single push reads back exactly.
  CHECK(f.push(42.0) == doctest::Approx(42.0));
  CHECK_FALSE(f.filled());
}

TEST_CASE("nexenne::filter::median very long run rejects intermittent spikes") {
  auto f{flt::median<double, 5>{}};
  auto last{0.0};
  for (auto i{0}; i < 1000; ++i) {
    auto const base{50.0};
    // Inject a wild spike every 7th sample; a 5-wide median rejects isolated
    // spikes outright because at most one of five samples is corrupted.
    auto const x{(i % 7 == 0) ? 1.0e9 : base};
    last = f.push(x);
    if (i > 10) {
      CHECK(last == doctest::Approx(base));
    }
  }
  CHECK(last == doctest::Approx(50.0));
}

TEST_CASE("nexenne::filter::median float instantiation matches double behaviour") {
  auto f{flt::median<float, 3>{}};
  static_cast<void>(f.push(10.0F));
  static_cast<void>(f.push(10.0F));
  static_cast<void>(f.push(10.0F));
  CHECK(f.push(1000.0F) == doctest::Approx(10.0));
  CHECK(f.value() == doctest::Approx(10.0));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::kalman first sample seeds the estimate (no transient)") {
  auto kf{flt::kalman{0.1, 0.5}};
  CHECK(kf.push(42.0) == doctest::Approx(42.0));  // unprimed: adopts measurement
  CHECK(kf.value() == doctest::Approx(42.0));
}

TEST_CASE("nexenne::filter::kalman converges to a constant measurement") {
  // Subsumes the legacy "converges to a constant measurement" case.
  auto kf{flt::kalman{0.01, 0.1}};
  for (auto i{0}; i < 50; ++i) {
    static_cast<void>(kf.push(42.0));
  }
  CHECK(kf.value() == doctest::Approx(42.0).epsilon(0.01));
}

TEST_CASE("nexenne::filter::kalman noisy constant converges to the true level") {
  // A deterministic +/- dither around 20 averages out toward 20.
  auto kf{flt::kalman{0.001, 1.0}};
  for (auto i{0}; i < 400; ++i) {
    auto const dither{(i % 2 == 0) ? 1.0 : -1.0};
    static_cast<void>(kf.push(20.0 + dither));
  }
  CHECK(kf.value() == doctest::Approx(20.0).epsilon(0.05));
}

TEST_CASE("nexenne::filter::kalman tracks a step change") {
  // Subsumes the legacy "tracks a step change" case.
  auto kf{flt::kalman{0.1, 0.5}};
  for (auto i{0}; i < 20; ++i) {
    static_cast<void>(kf.push(0.0));
  }
  for (auto i{0}; i < 50; ++i) {
    static_cast<void>(kf.push(100.0));
  }
  CHECK(kf.value() == doctest::Approx(100.0).epsilon(1.0));
}

TEST_CASE("nexenne::filter::kalman exact one-step update (Q=0, R=1, P0=1)") {
  // Prime with 0, then push 4:
  //   P_pred = P + Q = 1; denom = P_pred + R = 2; K = 1/2.
  //   x = 0 + 0.5*(4 - 0) = 2; P = (1 - 0.5)*1 = 0.5.
  auto kf{flt::kalman{0.0, 1.0, 0.0, 1.0}};
  CHECK(kf.push(0.0) == doctest::Approx(0.0));  // prime, P unchanged
  CHECK(kf.covariance() == doctest::Approx(1.0));
  auto const x{kf.push(4.0)};
  CHECK(x == doctest::Approx(2.0));
  CHECK(kf.covariance() == doctest::Approx(0.5));
}

TEST_CASE("nexenne::filter::kalman gain evolves as covariance shrinks") {
  // Q=0, R=1, P0=1.
  auto kf{flt::kalman{0.0, 1.0, 0.0, 1.0}};
  CHECK(kf.gain() == doctest::Approx(0.5));  // P_pred=1, denom=2 -> 0.5 (pre-prime)
  static_cast<void>(kf.push(10.0));          // prime: P stays 1
  CHECK(kf.gain() == doctest::Approx(0.5));
  static_cast<void>(kf.push(10.0));  // K=0.5 -> P=0.5; next gain: 0.5/1.5
  CHECK(kf.covariance() == doctest::Approx(0.5));
  CHECK(kf.gain() == doctest::Approx(1.0 / 3.0));
  static_cast<void>(kf.push(10.0));  // P=(2/3)*0.5 = 1/3
  CHECK(kf.covariance() == doctest::Approx(1.0 / 3.0));
}

TEST_CASE("nexenne::filter::kalman zero-variance / zero-denominator stays finite (NO NaN)") {
  // KNOWN-CORRECT audited guard: with Q=0, R=0 and P0=0 the gain denominator
  // P + Q + R is zero. The guard sets K=0 (trust the prediction) instead of
  // dividing by zero, so the estimate is held and never becomes NaN/inf.
  auto kf{flt::kalman{0.0, 0.0, 0.0, 0.0}};
  CHECK(kf.gain() == doctest::Approx(0.0));  // denom == 0 -> guarded to 0
  auto const a{kf.push(5.0)};                // prime -> estimate becomes 5
  CHECK(a == doctest::Approx(5.0));
  CHECK(std::isfinite(a));
  auto const b{kf.push(7.0)};  // denom 0 -> K=0 -> estimate held at 5
  CHECK(std::isfinite(b));
  CHECK(b == doctest::Approx(5.0));
  CHECK(std::isfinite(kf.value()));
  CHECK(std::isfinite(kf.covariance()));
  CHECK(kf.gain() == doctest::Approx(0.0));
  // A long run of the degenerate case must stay finite throughout.
  for (auto i{0}; i < 100; ++i) {
    auto const v{kf.push(static_cast<double>(i))};
    CHECK(std::isfinite(v));
    CHECK(v == doctest::Approx(5.0));  // estimate never moves (K stays 0)
  }
}

TEST_CASE("nexenne::filter::kalman process vs measurement noise change convergence speed") {
  // Two filters tracking a 0->100 step. A higher process-noise (more agile)
  // filter must reach the new level faster than a sluggish, high-R one.
  auto agile{flt::kalman{1.0, 0.1}};         // trusts measurements, adapts quickly
  auto sluggish{flt::kalman{0.001, 100.0}};  // trusts model, adapts slowly
  static_cast<void>(agile.push(0.0));
  static_cast<void>(sluggish.push(0.0));
  for (auto i{0}; i < 10; ++i) {
    static_cast<void>(agile.push(100.0));
    static_cast<void>(sluggish.push(100.0));
  }
  CHECK(agile.value() > sluggish.value());
  CHECK(agile.value() == doctest::Approx(100.0).epsilon(0.05));
  CHECK(sluggish.value() < 50.0);  // still far from the new level
}

TEST_CASE("nexenne::filter::kalman noise() swaps parameters without touching state") {
  auto kf{flt::kalman{0.0, 1.0, 0.0, 1.0}};
  static_cast<void>(kf.push(10.0));  // prime, estimate=10, P=1
  CHECK(kf.gain() == doctest::Approx(0.5));
  kf.noise(0.0, 3.0);                              // R now 3 -> gain = 1/(1+3) = 0.25
  CHECK(kf.value() == doctest::Approx(10.0));      // estimate untouched
  CHECK(kf.covariance() == doctest::Approx(1.0));  // covariance untouched
  CHECK(kf.gain() == doctest::Approx(0.25));
}

TEST_CASE("nexenne::filter::kalman reset reseeds estimate, covariance, and priming") {
  auto kf{flt::kalman{0.1, 0.5}};
  for (auto i{0}; i < 30; ++i) {
    static_cast<void>(kf.push(50.0));
  }
  CHECK(kf.value() == doctest::Approx(50.0).epsilon(0.1));
  kf.reset(5.0, 2.0);
  CHECK(kf.value() == doctest::Approx(5.0));
  CHECK(kf.covariance() == doctest::Approx(2.0));
  // Next push reseeds the estimate directly from the measurement.
  CHECK(kf.push(99.0) == doctest::Approx(99.0));

  // Default reset arguments restore estimate 0, covariance 1.
  kf.reset();
  CHECK(kf.value() == doctest::Approx(0.0));
  CHECK(kf.covariance() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::filter::kalman all-equal input keeps the estimate pinned") {
  auto kf{flt::kalman{0.01, 0.1}};
  static_cast<void>(kf.push(7.0));
  for (auto i{0}; i < 200; ++i) {
    auto const v{kf.push(7.0)};
    CHECK(v == doctest::Approx(7.0));
  }
}

TEST_CASE("nexenne::filter::kalman float instantiation seeds and converges") {
  auto kf{flt::kalman<float>{0.01F, 0.1F}};
  CHECK(kf.push(3.0F) == doctest::Approx(3.0));  // prime
  for (auto i{0}; i < 50; ++i) {
    static_cast<void>(kf.push(3.0F));
  }
  CHECK(kf.value() == doctest::Approx(3.0).epsilon(0.01));
  CHECK(std::isfinite(kf.value()));
}

TEST_CASE("nexenne::filter::complementary blends two sensors by alpha") {
  // Subsumes the legacy "blends two sensors" case.
  auto cf{flt::complementary{0.98}};
  CHECK(cf.alpha() == doctest::Approx(0.98));
  auto const y{cf.push(10.0, 9.5)};
  // 0.98 * 10 + 0.02 * 9.5 = 9.99
  CHECK(y == doctest::Approx(9.99));
  CHECK(cf.value() == doctest::Approx(9.99));
}

TEST_CASE("nexenne::filter::complementary alpha=1 selects the fast source exactly") {
  auto cf{flt::complementary{1.0}};
  CHECK(cf.push(12.0, -99.0) == doctest::Approx(12.0));  // 1*fast + 0*slow
  CHECK(cf.push(3.5, 1000.0) == doctest::Approx(3.5));
}

TEST_CASE("nexenne::filter::complementary alpha=0 selects the slow source exactly") {
  auto cf{flt::complementary{0.0}};
  CHECK(cf.push(-99.0, 12.0) == doctest::Approx(12.0));  // 0*fast + 1*slow
  CHECK(cf.push(1000.0, 3.5) == doctest::Approx(3.5));
}

TEST_CASE("nexenne::filter::complementary intermediate alpha is the exact weighted sum") {
  auto cf{flt::complementary{0.25}};
  // 0.25 * 8 + 0.75 * 4 = 2 + 3 = 5
  CHECK(cf.push(8.0, 4.0) == doctest::Approx(5.0));
  // 0.25 * -4 + 0.75 * 8 = -1 + 6 = 5
  CHECK(cf.push(-4.0, 8.0) == doctest::Approx(5.0));
  // 0.25 * 100 + 0.75 * 0 = 25
  CHECK(cf.push(100.0, 0.0) == doctest::Approx(25.0));
}

TEST_CASE("nexenne::filter::complementary single-input overload is a first-order low-pass") {
  // The single-argument push treats the sample as fast and the previous output
  // as slow: y = alpha*sample + (1-alpha)*y_prev. A constant input settles to it.
  auto cf{flt::complementary{0.5}};
  CHECK(cf.value() == doctest::Approx(0.0));
  CHECK(cf.push(10.0) == doctest::Approx(5.0));   // 0.5*10 + 0.5*0
  CHECK(cf.push(10.0) == doctest::Approx(7.5));   // 0.5*10 + 0.5*5
  CHECK(cf.push(10.0) == doctest::Approx(8.75));  // 0.5*10 + 0.5*7.5
  for (auto i{0}; i < 100; ++i) {
    static_cast<void>(cf.push(10.0));
  }
  CHECK(cf.value() == doctest::Approx(10.0).epsilon(0.001));  // converges to input
}

TEST_CASE("nexenne::filter::complementary high/low-pass split sums to the inputs at extremes") {
  // y = alpha*fast + (1-alpha)*slow. When fast == slow the blend is exact for
  // any alpha (the two-band split reconstructs a shared DC level).
  auto cf{flt::complementary{0.7}};
  CHECK(cf.push(5.0, 5.0) == doctest::Approx(5.0));
  cf.alpha(0.3);
  CHECK(cf.push(-2.0, -2.0) == doctest::Approx(-2.0));
}

TEST_CASE("nexenne::filter::complementary alpha() setter changes subsequent blends") {
  auto cf{flt::complementary{0.5}};
  CHECK(cf.push(10.0, 0.0) == doctest::Approx(5.0));
  cf.alpha(0.9);
  CHECK(cf.alpha() == doctest::Approx(0.9));
  CHECK(cf.push(10.0, 0.0) == doctest::Approx(9.0));  // new weight applied
}

TEST_CASE("nexenne::filter::complementary reset variants") {
  auto cf{flt::complementary{0.5}};
  static_cast<void>(cf.push(10.0, 10.0));
  CHECK(cf.value() == doctest::Approx(10.0));
  cf.reset();
  CHECK(cf.value() == doctest::Approx(0.0));  // reset() -> zero
  // The single-arg reset seeds the stored output.
  cf.reset(3.0);
  CHECK(cf.value() == doctest::Approx(3.0));
  // The seeded value participates in the next low-pass blend.
  CHECK(cf.push(3.0) == doctest::Approx(3.0));  // 0.5*3 + 0.5*3
  // alpha survives a reset.
  CHECK(cf.alpha() == doctest::Approx(0.5));
}

TEST_CASE("nexenne::filter::complementary alternating extremes blend deterministically") {
  auto cf{flt::complementary{0.5}};
  CHECK(cf.push(1e6, -1e6) == doctest::Approx(0.0));  // average of +/- 1e6
  CHECK(cf.push(-1e6, 1e6) == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::complementary float instantiation") {
  auto cf{flt::complementary<float>{0.25F}};
  CHECK(cf.push(8.0F, 4.0F) == doctest::Approx(5.0));
  cf.reset();
  CHECK(cf.value() == doctest::Approx(0.0));
  CHECK(cf.push(4.0F, 8.0F) == doctest::Approx(7.0));  // 0.25*4 + 0.75*8
}

TEST_CASE("nexenne::filter::lms default construction reports zero taps and the default step") {
  // Subsumes the legacy default-construction case.
  auto f{flt::lms<double, 4>{}};
  CHECK(f.step_size() == doctest::Approx(0.01));
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(0.0));
  // With zero taps the first output is zero, error equals the desired.
  auto const y{f.push(1.0, 5.0)};
  CHECK(y == doctest::Approx(0.0));
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(5.0));
}

TEST_CASE("nexenne::filter::lms hand-computed two-step update (N=2, mu=0.1)") {
  // Start: taps {0,0}, history {0,0}, idx 0.
  // push(2, 5): out = 0, err = 5; only the freshest tap sees x=2.
  //   c0 += 0.1*5*2 = 1.0; c1 += 0.1*5*0 = 0.   -> c = {1.0, 0.0}
  // push(3, 7): out = c0*3 + c1*2 = 3; err = 4.
  //   c0 += 0.1*4*3 = 1.2 -> 2.2; c1 += 0.1*4*2 = 0.8 -> 0.8.
  auto f{flt::lms<double, 2>{0.1}};
  auto const y0{f.push(2.0, 5.0)};
  CHECK(y0 == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(5.0));
  CHECK(f.coefficients()[0] == doctest::Approx(1.0));
  CHECK(f.coefficients()[1] == doctest::Approx(0.0));

  auto const y1{f.push(3.0, 7.0)};
  CHECK(y1 == doctest::Approx(3.0));
  CHECK(f.value() == doctest::Approx(3.0));
  CHECK(f.error() == doctest::Approx(4.0));
  CHECK(f.coefficients()[0] == doctest::Approx(2.2));
  CHECK(f.coefficients()[1] == doctest::Approx(0.8));
}

TEST_CASE("nexenne::filter::lms identifies a simple gain system (stable mu)") {
  // Subsumes the legacy gain-identification case. Desired d[n] = 3*x[n].
  // Inputs are bounded so 0 < mu < 2/input_power holds and the tap converges.
  auto f{flt::lms<double, 1>{0.1}};
  auto x{1.0};
  for (auto i{0}; i < 500; ++i) {
    x = (i % 2 == 0) ? 1.0 : 0.5;
    auto const d{3.0 * x};
    static_cast<void>(f.push(x, d));
  }
  CHECK(f.coefficients()[0] == doctest::Approx(3.0).epsilon(0.05));
  CHECK(std::abs(f.error()) < 0.05);
}

TEST_CASE("nexenne::filter::lms error shrinks over iterations as it adapts") {
  // Subsumes the legacy "error decreases" case. 2-tap target d = 2*x + 1*x[-1].
  auto f{flt::lms<double, 2>{0.05}};
  auto prev{0.0};
  auto first_abs_err{0.0};
  auto last_abs_err{0.0};
  for (auto i{0}; i < 2000; ++i) {
    auto const x{
      std::sin(0.3 * static_cast<double>(i)) + 0.5 * std::cos(0.11 * static_cast<double>(i))
    };
    auto const d{2.0 * x + 1.0 * prev};
    static_cast<void>(f.push(x, d));
    if (i == 5) {
      first_abs_err = std::abs(f.error());
    }
    if (i == 1999) {
      last_abs_err = std::abs(f.error());
    }
    prev = x;
  }
  CHECK(last_abs_err < first_abs_err);
  CHECK(f.coefficients()[0] == doctest::Approx(2.0).epsilon(0.1));
  CHECK(f.coefficients()[1] == doctest::Approx(1.0).epsilon(0.1));
}

TEST_CASE("nexenne::filter::lms a stable mu drives the running error toward zero") {
  // With a constant drive x=1, input power is 1 so any 0 < mu < 2 is stable.
  // A 1-tap filter must converge to the target gain and the error must shrink.
  auto f{flt::lms<double, 1>{0.2}};
  auto const target{4.0};
  auto early_err{0.0};
  auto late_err{0.0};
  for (auto i{0}; i < 200; ++i) {
    static_cast<void>(f.push(1.0, target));
    if (i == 1) {
      early_err = std::abs(f.error());
    }
    if (i == 199) {
      late_err = std::abs(f.error());
    }
  }
  CHECK(late_err < early_err);
  CHECK(f.coefficients()[0] == doctest::Approx(target).epsilon(0.01));
  CHECK(std::abs(f.error()) < 0.01);
}

TEST_CASE("nexenne::filter::lms step_size getter/setter") {
  auto f{flt::lms<double, 3>{}};
  CHECK(f.step_size() == doctest::Approx(0.01));  // default
  f.step_size(0.2);
  CHECK(f.step_size() == doctest::Approx(0.2));
  // The explicit step-size constructor sets it directly.
  auto g{flt::lms<double, 3>{0.05}};
  CHECK(g.step_size() == doctest::Approx(0.05));
}

TEST_CASE("nexenne::filter::lms coefficients() exposes N taps") {
  auto f{flt::lms<double, 5>{}};
  CHECK(f.coefficients().size() == std::size_t{5});
  static_assert(decltype(f.coefficients())::extent == std::size_t{5});
}

TEST_CASE("nexenne::filter::lms reset clears taps and history but preserves the step size") {
  // Subsumes the legacy reset case.
  auto f{flt::lms<double, 2>{0.07}};
  for (auto i{0}; i < 50; ++i) {
    static_cast<void>(f.push(1.0, 2.0));
  }
  CHECK(f.coefficients()[0] != doctest::Approx(0.0));  // adapted away from zero
  f.reset();
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(0.0));
  CHECK(f.step_size() == doctest::Approx(0.07));  // step preserved
  // After reset, history is silent: first output is zero again.
  CHECK(f.push(2.0, 6.0) == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(6.0));
}

TEST_CASE("nexenne::filter::lms all-zero desired keeps taps at zero from a zero start") {
  auto f{flt::lms<double, 3>{0.1}};
  for (auto i{0}; i < 100; ++i) {
    static_cast<void>(f.push(1.0, 0.0));
  }
  // Output starts at 0, desired is 0, so the error is 0 and no tap ever moves.
  for (auto const c : f.coefficients()) {
    CHECK(c == doctest::Approx(0.0));
  }
  CHECK(f.error() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::filter::lms very long stable run stays finite and converged") {
  auto f{flt::lms<double, 4>{0.05}};
  auto prev{std::array<double, 3>{0.0, 0.0, 0.0}};
  for (auto i{0}; i < 20000; ++i) {
    auto const x{std::sin(0.2 * static_cast<double>(i))};
    // 3-tap target embedded in a 4-tap filter (4th tap should fade to ~0).
    auto const d{1.5 * x + 0.5 * prev[0] - 0.25 * prev[1]};
    static_cast<void>(f.push(x, d));
    prev[2] = prev[1];
    prev[1] = prev[0];
    prev[0] = x;
  }
  for (auto const c : f.coefficients()) {
    CHECK(std::isfinite(c));
  }
  CHECK(std::isfinite(f.value()));
  CHECK(std::abs(f.error()) < 0.05);
}

TEST_CASE("nexenne::filter::lms float instantiation hand-check") {
  auto f{flt::lms<float, 2>{0.1F}};
  CHECK(f.push(2.0F, 5.0F) == doctest::Approx(0.0));
  CHECK(f.error() == doctest::Approx(5.0));
  CHECK(f.coefficients()[0] == doctest::Approx(1.0));
  CHECK(f.push(3.0F, 7.0F) == doctest::Approx(3.0));
  CHECK(f.coefficients()[0] == doctest::Approx(2.2));
  CHECK(f.coefficients()[1] == doctest::Approx(0.8));
  f.reset();
  CHECK(f.value() == doctest::Approx(0.0));
  CHECK(f.step_size() == doctest::Approx(0.1));
}

}  // namespace
