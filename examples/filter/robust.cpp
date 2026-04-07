/**
 * @file
 * @brief Nonlinear, statistical, fusion, and adaptive filters.
 *
 * Shows a median rejecting spikes a mean would smear (and what its window size
 * buys), a scalar Kalman filter converging on a noisy constant while its gain
 * and covariance shrink, a complementary filter fusing a fast and a slow source,
 * and an LMS filter identifying an unknown gain online and tracking it when it
 * changes.
 */

#include <cmath>
#include <print>

#include <nexenne/filter/adaptive.hpp>
#include <nexenne/filter/complementary.hpp>
#include <nexenne/filter/kalman.hpp>
#include <nexenne/filter/median.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace flt = nexenne::filter;

}  // namespace

auto main() -> int {
  // 1. Median of 3: a lone 99 in a run of 10s never reaches the output. Unlike
  // the linear smoothers, the median is nonlinear, so an isolated outlier is
  // deleted outright rather than averaged in.
  auto med{flt::median<double, 3>{}};
  std::print("1. median(3) of 10,10,99,10,10 ->");
  for (auto const x : {10.0, 10.0, 99.0, 10.0, 10.0}) {
    std::print(" {:.0f}", med.push(x));
  }
  std::println("");

  // 2. Window size is the spike-rejection budget. A width-N median survives up
  // to (N-1)/2 consecutive bad samples; a width-3 is fooled by two spikes in a
  // row, while a width-5 still rides through them. We feed both a clean 10 with a
  // two-sample 99 burst.
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

  // 3. Kalman on a constant 42 measured with alternating +/-2 noise. As more
  // measurements arrive the filter grows confident: the covariance P (its own
  // uncertainty) and the gain (how much it trusts each new reading) both shrink,
  // so later samples move the estimate less. We print the trajectory.
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

  // 4. Q is the "how fast can the truth move?" knob. A larger process noise Q
  // keeps the gain high, so the filter tracks a genuine ramp faster at the cost
  // of admitting more measurement noise. We push a rising ramp and report how
  // far each estimate lags the input on the last sample.
  std::println("4. kalman process-noise Q vs. tracking lag on a ramp:");
  for (auto const q : {0.001, 0.05, 0.5}) {
    auto k{flt::kalman{q, 1.0}};
    auto y{0.0};
    auto in{0.0};
    for (auto n{0}; n < 30; ++n) {
      in = static_cast<double>(n);  // a clean unit-per-sample ramp
      y = k.push(in);
    }
    std::println("   Q {:.3f}: final estimate {:6.3f} vs input {:.1f}", q, y, in);
  }

  // 5. Complementary fusion: trust a fast (but drifty) source at 0.95 and a slow
  // (but stable) one at 0.05. The two-argument push blends them directly; the
  // single-argument overload instead blends the new sample with the previous
  // output, degenerating into a plain first-order low-pass.
  auto cf{flt::complementary{0.95}};
  std::println(
    "5. complementary(0.95) of fast=10.0 slow=8.0: {:.3f} (two-sensor fusion)", cf.push(10.0, 8.0)
  );

  // 6. LMS identifies an unknown gain of 3 from input/desired pairs, then we
  // change the true gain to 5 mid-stream. Because it adapts every sample, the
  // filter re-converges on the new plant - the property that makes it useful for
  // echo cancellation and channel equalisation where the system drifts.
  std::println("6. lms tracking a gain that changes 3 -> 5 mid-stream:");
  auto lms{flt::lms<double, 1>{0.1}};
  for (auto n{0}; n < 500; ++n) {
    auto const in{(n % 2 == 0) ? 1.0 : 0.5};
    nexenne::utility::discard(lms.push(in, 3.0 * in));
  }
  std::println("   after 500 samples at gain 3: {:.3f}", lms.coefficients()[0]);
  for (auto n{0}; n < 500; ++n) {
    auto const in{(n % 2 == 0) ? 1.0 : 0.5};
    nexenne::utility::discard(lms.push(in, 5.0 * in));
  }
  std::println("   after 500 more at gain 5: {:.3f} (re-converged)", lms.coefficients()[0]);
  return 0;
}
