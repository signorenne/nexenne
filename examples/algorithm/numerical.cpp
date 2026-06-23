/**
 * @file
 * @brief Example: the nexenne::algorithm numerical routines.
 *
 * Shows compensated summation (kahan vs neumaier), root finding with error
 * handling, two quadrature rules, ODE stepping (euler vs rk4), online statistics
 * (running_stats with merge, a histogram with quantiles, an exponential moving
 * estimate), interpolation, and the FFT. Every fallible call is handled inline.
 */

#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <span>
#include <vector>

#include <nexenne/algorithm/numerical/bisection.hpp>
#include <nexenne/algorithm/numerical/fft.hpp>
#include <nexenne/algorithm/numerical/integration.hpp>
#include <nexenne/algorithm/numerical/interpolation.hpp>
#include <nexenne/algorithm/numerical/kahan_sum.hpp>
#include <nexenne/algorithm/numerical/numerical_error.hpp>
#include <nexenne/algorithm/numerical/ode.hpp>
#include <nexenne/algorithm/numerical/online_stats.hpp>
#include <nexenne/utility/discard.hpp>

namespace alg = nexenne::algorithm;

auto main() -> int {
  // Compensated summation recovers the small terms naive addition drops. With
  // 1 + 1e16 + 1 - 1e16, a plain double sum loses both 1s and yields 0. Plain
  // Kahan also loses them here, because its correction is dropped when the next
  // addend (1e16) dwarfs the running sum; Neumaier's variant handles exactly that
  // case (it compares magnitudes) and recovers the 2. Reach for neumaier_sum when
  // the summands swing widely in magnitude.
  auto const tricky{std::array<double, 4>{1.0, 1e16, 1.0, -1e16}};
  std::printf(
    "kahan_sum        = %.1f  (correction lost: addend dwarfs sum)\n", alg::kahan_sum(tricky)
  );
  std::printf("neumaier_sum     = %.1f  (recovers the small terms)\n", alg::neumaier_sum(tricky));

  // bisection returns expected<T, numerical_error>. A valid bracket (f changes
  // sign over [0, 2]) finds sqrt(2). We unwrap explicitly to show the success path.
  if (auto const root{alg::bisection<double>([](double x) { return x * x - 2.0; }, 0.0, 2.0)}) {
    std::printf("bisection sqrt2  = %.9f\n", *root);
  }

  // The error case: with both endpoints positive, f does not change sign, so
  // there is no guaranteed root and the call reports not_bracketed rather than
  // looping. Separating "bad input" from a real answer is the point of expected.
  auto const bad{alg::bisection<double>([](double x) { return x * x + 1.0; }, 0.0, 2.0)};
  std::printf(
    "bisection (no root) -> %s\n",
    bad.has_value()
      ? "value"
      : (bad.error() == alg::numerical_error::not_bracketed ? "not_bracketed" : "other")
  );

  // Quadrature of x^2 over [0, 1] = 1/3. trapezoidal is 2nd-order; simpson is
  // 4th-order and exact for polynomials up to cubic, so it nails 1/3 with few
  // panels where the trapezoidal rule still carries a small bias.
  auto const sq{[](double x) { return x * x; }};
  std::printf("trapezoidal x^2  = %.9f  (8 panels)\n", alg::trapezoidal(sq, 0.0, 1.0, 8));
  std::printf("simpson x^2      = %.9f  (8 panels, exact)\n", alg::simpson(sq, 0.0, 1.0, 8));

  // ODE integration of y' = y, y(0) = 1, whose exact solution at t = 1 is e.
  // One Euler step is crude (first order); one RK4 step uses four derivative
  // samples and lands far closer at the same step size. Both are O(1) per step.
  auto const dydt{[](double, double y) { return y; }};
  std::printf("euler  e (1 step)= %.6f\n", alg::euler_step<double>(dydt, 0.0, 1.0, 1.0));
  std::printf(
    "rk4    e (1 step)= %.6f  (true e = %.6f)\n",
    alg::rk4_step<double>(dydt, 0.0, 1.0, 1.0),
    std::exp(1.0)
  );

  // running_stats (Welford): mean, variance, stddev, min, max in one streaming
  // pass, O(1) per sample and numerically stable. Two partial accumulators merge
  // in closed form, the basis of a parallel reduction.
  auto rs{alg::running_stats<double>{}};
  for (auto const x : {2.0, 4.0, 4.0, 4.0}) {
    rs.push(x);
  }
  auto rs2{alg::running_stats<double>{}};
  for (auto const x : {5.0, 5.0, 7.0, 9.0}) {
    rs2.push(x);
  }
  rs.merge(rs2);  // same result as pushing all eight into one accumulator
  std::printf("merged mean/std  = %.3f / %.3f  (n = %zu)\n", rs.mean(), rs.stddev(), rs.count());

  // A fixed-bucket histogram over [0, 10] in 5 buckets, with dedicated underflow
  // and overflow bins so no sample is lost. quantile(p) reads back an approximate
  // percentile from the counts without storing the samples.
  auto hist{alg::histogram<double, 5>{0.0, 10.0}};
  for (auto const x : {1.0, 2.0, 2.0, 3.0, 7.0, 8.0, 9.0, 9.5}) {
    hist.push(x);
  }
  std::printf(
    "histogram median ~ %.1f  (total %llu samples)\n",
    hist.quantile(0.5),
    static_cast<unsigned long long>(hist.total())
  );

  // ema_stats is an exponential moving estimate: it weights recent samples more,
  // tracking a drifting signal where a plain running mean would lag. alpha = 0.3
  // here; after a level shift the estimate chases the new value.
  auto ema{alg::ema_stats<double>{0.3}};
  for (auto const x : {10.0, 10.0, 10.0, 20.0, 20.0, 20.0}) {
    ema.push(x);
  }
  std::printf("ema mean (drift) = %.3f  (chasing the shift to 20)\n", ema.mean());

  // Piecewise-linear interpolation of y = x^2 samples; lin(1.5) sits on the chord
  // between the (1, 1) and (2, 4) knots, so 2.5, not the true 2.25.
  auto const xs{std::array<double, 3>{0.0, 1.0, 2.0}};
  auto const ys{std::array<double, 3>{0.0, 1.0, 4.0}};
  auto const lin{alg::linear_interpolator<double>{xs, ys}};
  std::printf("lerp table @1.5  = %.3f\n", lin(1.5));

  // FFT round-trip: forward then inverse returns the original signal (to rounding).
  auto sig{std::vector<std::complex<double>>{{1, 0}, {2, 0}, {3, 0}, {4, 0}}};
  auto const original{sig};
  nexenne::utility::discard(alg::fft<double>(std::span<std::complex<double>>{sig}));
  nexenne::utility::discard(alg::ifft<double>(std::span<std::complex<double>>{sig}));
  std::printf(
    "fft round-trip ok: %s\n",
    (std::abs(sig[0] - original[0]) < 1e-9 && std::abs(sig[3] - original[3]) < 1e-9) ? "yes" : "no"
  );
  return 0;
}
