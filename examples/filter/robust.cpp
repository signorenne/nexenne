/**
 * @file
 * @brief Nonlinear, statistical, fusion, and adaptive filters.
 *
 * Shows a median rejecting a spike a mean would smear, a scalar Kalman filter
 * converging on a noisy constant, a complementary filter fusing a fast and a
 * slow source, and an LMS filter identifying an unknown gain online.
 */

#include <cmath>
#include <print>

#include <nexenne/filter/adaptive.hpp>
#include <nexenne/filter/complementary.hpp>
#include <nexenne/filter/kalman.hpp>
#include <nexenne/filter/median.hpp>

namespace {

namespace flt = nexenne::filter;

}  // namespace

auto main() -> int {
  // Median of 3: a lone 99 in a run of 10s never reaches the output.
  auto med{flt::median<double, 3>{}};
  std::print("median(3) of 10,10,99,10,10 ->");
  for (auto const x : {10.0, 10.0, 99.0, 10.0, 10.0}) {
    std::print(" {:.0f}", med.push(x));
  }
  std::println("");

  // Kalman on a constant 42 measured with alternating +/-2 noise.
  auto kf{flt::kalman{0.01, 1.0}};
  auto estimate{0.0};
  for (auto n{0}; n < 30; ++n) {
    auto const noise{(n % 2 == 0) ? 2.0 : -2.0};
    estimate = kf.push(42.0 + noise);
  }
  std::println("kalman estimate after 30 noisy reads of 42: {:.3f}", estimate);

  // Complementary fusion: trust a fast source at 0.95, a slow one at 0.05.
  auto cf{flt::complementary{0.95}};
  std::println("complementary(0.95) of fast=10.0 slow=8.0: {:.3f}", cf.push(10.0, 8.0));

  // LMS identifies an unknown gain of 3 from input/desired pairs.
  auto lms{flt::lms<double, 1>{0.1}};
  for (auto n{0}; n < 500; ++n) {
    auto const in{(n % 2 == 0) ? 1.0 : 0.5};
    static_cast<void>(lms.push(in, 3.0 * in));
  }
  std::println("lms identified gain (target 3.0): {:.3f}", lms.coefficients()[0]);
}
