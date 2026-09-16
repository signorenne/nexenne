/**
 * @file
 * @brief Nonlinear, statistical, fusion, and adaptive filters.
 *
 * Shows a median rejecting spikes a mean would smear (and what its window size
 * buys), a scalar Kalman filter converging on a noisy constant while its gain
 * and covariance shrink, a complementary filter fusing a fast and a slow source,
 * and an LMS filter identifying an unknown gain online and tracking it when it
 * changes.
 *
 * The program walks six steps:
 *
 * 1. A median of 3 deletes a lone 99 in a run of 10s outright: being nonlinear,
 *    it never averages the outlier in.
 * 2. The window size is the spike-rejection budget: a width-N median survives up
 *    to (N-1)/2 consecutive bad samples, so a two-sample burst fools N = 3 but
 *    not N = 5.
 * 3. A Kalman filter on a constant 42 with alternating +/-2 noise grows
 *    confident: its covariance P and its gain both shrink, so later samples move
 *    the estimate less.
 * 4. The process noise Q is the "how fast can the truth move" knob: a larger Q
 *    keeps the gain high and tracks a clean unit-per-sample ramp with less lag,
 *    at the cost of admitting more measurement noise.
 * 5. Complementary fusion trusts a fast, drifty source at 0.95 and a slow, stable
 *    one at 0.05; the two-argument push blends them directly, while the
 *    single-argument overload blends with the previous output and degenerates
 *    into a first-order low-pass.
 * 6. LMS identifies an unknown gain of 3, then re-converges when the true gain
 *    changes to 5, the property that suits it to echo cancellation and channel
 *    equalisation where the system drifts.
 */

#include <cmath>
#include <print>

#include <nexenne/filter/adaptive.hpp>
#include <nexenne/filter/complementary.hpp>
#include <nexenne/filter/kalman.hpp>
#include <nexenne/filter/median.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace flt = nexenne::filter;

}  // namespace

auto main() -> int {
  auto med{flt::median<double, 3>{}};
  std::print("1. median(3) of 10,10,99,10,10 ->");
  for (auto const x : {10.0, 10.0, 99.0, 10.0, 10.0}) {
    std::print(" {:.0f}", med.push(x));
  }
  std::println("");

  auto med3{flt::median<double, 3>{}};
  auto med5{flt::median<double, 5>{}};
  std::print("2. two-sample burst (10,10,99,99,10,10):  N=3 ->");
  for (auto const x : {10.0, 10.0, 99.0, 99.0, 10.0, 10.0}) {
    std::print(" {:.0f}", med3.push(x));
  }
  std::print(" | N=5 ->");
  for (auto const x : {10.0, 10.0, 99.0, 99.0, 10.0, 10.0}) {
    std::print(" {:.0f}", med5.push(x));
  }
  std::println("  (N=3 lets the pair through, N=5 holds)");

  std::println("3. kalman converging on a noisy constant 42:");
  auto kf{flt::kalman{0.01, 1.0}};
  std::println("   {:>2}  {:>8}  {:>6}  {:>8}", "n", "estimate", "gain", "cov P");
  auto estimate{0.0};
  for (auto n{0}; n < 12; ++n) {
    auto const noise{(n % 2 == 0) ? 2.0 : -2.0};
    estimate = kf.push(42.0 + noise);
    if (n < 4 || n == 11) {
      std::println("   {:2}  {:8.4f}  {:6.4f}  {:8.4f}", n, estimate, kf.gain(), kf.covariance());
    }
  }

  std::println("4. kalman process-noise Q vs. tracking lag on a ramp:");
  for (auto const q : {0.001, 0.05, 0.5}) {
    auto k{flt::kalman{q, 1.0}};
    auto y{0.0};
    auto in{0.0};
    for (auto n{0}; n < 30; ++n) {
      in = static_cast<double>(n);
      y = k.push(in);
    }
    std::println("   Q {:.3f}: final estimate {:6.3f} vs input {:.1f}", q, y, in);
  }

  auto cf{flt::complementary{0.95}};
  std::println(
    "5. complementary(0.95) of fast=10.0 slow=8.0: {:.3f} (two-sensor fusion)", cf.push(10.0, 8.0)
  );

  std::println("6. lms tracking a gain that changes 3 -> 5 mid-stream:");
  auto lms{flt::lms<double, 1>{0.1}};
  for (auto n{0}; n < 500; ++n) {
    auto const in{(n % 2 == 0) ? 1.0 : 0.5};
    nexenne::utility::ignore(lms.push(in, 3.0 * in));
  }
  std::println("   after 500 samples at gain 3: {:.3f}", lms.coefficients()[0]);
  for (auto n{0}; n < 500; ++n) {
    auto const in{(n % 2 == 0) ? 1.0 : 0.5};
    nexenne::utility::ignore(lms.push(in, 5.0 * in));
  }
  std::println("   after 500 more at gain 5: {:.3f} (re-converged)", lms.coefficients()[0]);
  return 0;
}
