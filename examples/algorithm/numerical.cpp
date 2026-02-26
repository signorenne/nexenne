/**
 * @file
 * @brief Example: the nexenne::algorithm numerical routines.
 *
 * Shows compensated summation, root finding, quadrature, online statistics,
 * interpolation, and the FFT.
 */

#include <array>
#include <complex>
#include <cstdio>
#include <span>
#include <vector>

#include <nexenne/algorithm/numerical/bisection.hpp>
#include <nexenne/algorithm/numerical/fft.hpp>
#include <nexenne/algorithm/numerical/integration.hpp>
#include <nexenne/algorithm/numerical/interpolation.hpp>
#include <nexenne/algorithm/numerical/kahan_sum.hpp>
#include <nexenne/algorithm/numerical/online_stats.hpp>

namespace alg = nexenne::algorithm;

auto main() -> int {
  // Compensated summation recovers the small term naive addition would drop.
  auto const tricky{std::array<double, 4>{1.0, 1e16, 1.0, -1e16}};
  std::printf("neumaier_sum     = %.1f\n", alg::neumaier_sum(tricky));

  // Root of x^2 - 2 by bisection.
  auto const root{alg::bisection<double>([](double x) { return x * x - 2.0; }, 0.0, 2.0)};
  std::printf("bisection sqrt2  = %.9f\n", root.value_or(0.0));

  // Integral of x^2 over [0, 1] = 1/3.
  std::printf(
    "simpson x^2 0..1 = %.9f\n", alg::simpson([](double x) { return x * x; }, 0.0, 1.0, 100)
  );

  // Online statistics.
  auto rs{alg::running_stats<double>{}};
  for (auto const x : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) {
    rs.push(x);
  }
  std::printf("running mean/std = %.3f / %.3f\n", rs.mean(), rs.stddev());

  // Piecewise-linear interpolation of y = x^2 samples.
  auto const xs{std::array<double, 3>{0.0, 1.0, 2.0}};
  auto const ys{std::array<double, 3>{0.0, 1.0, 4.0}};
  auto const lin{alg::linear_interpolator<double>{xs, ys}};
  std::printf("lerp table @1.5  = %.3f\n", lin(1.5));

  // FFT round-trip.
  auto sig{std::vector<std::complex<double>>{{1, 0}, {2, 0}, {3, 0}, {4, 0}}};
  auto const original{sig};
  static_cast<void>(alg::fft<double>(std::span<std::complex<double>>{sig}));
  static_cast<void>(alg::ifft<double>(std::span<std::complex<double>>{sig}));
  std::printf(
    "fft round-trip ok: %s\n",
    (std::abs(sig[0] - original[0]) < 1e-9 && std::abs(sig[3] - original[3]) < 1e-9) ? "yes" : "no"
  );
  return 0;
}
