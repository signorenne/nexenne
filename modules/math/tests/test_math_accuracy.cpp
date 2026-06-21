// Accuracy harness: sweeps every approximation across its documented range and
// asserts the worst-case error stays within the bound the docs promise. This is
// what pins the in-code accuracy claims (see the math-in-code documentation
// standard) so an overstated number or a regression fails the build, unlike the
// loose spot checks in the per-function suites.

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

#include <nexenne/math/power.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/trigonometry.hpp>

namespace math = nexenne::math;

namespace {

// Max over a dense sweep of [lo, hi] of |approx(x) - exact(x)|.
template <typename Approx, typename Exact>
auto max_abs_error(double lo, double hi, int samples, Approx approx, Exact exact) -> double {
  double worst{0.0};
  for (int i{0}; i <= samples; ++i) {
    double const x{lo + (hi - lo) * i / samples};
    worst = std::max(worst, std::abs(approx(x) - exact(x)));
  }
  return worst;
}

template <typename Approx, typename Exact>
auto max_rel_error(double lo, double hi, int samples, Approx approx, Exact exact) -> double {
  double worst{0.0};
  for (int i{0}; i <= samples; ++i) {
    double const x{lo + (hi - lo) * i / samples};
    double const ref{exact(x)};
    worst = std::max(worst, std::abs(approx(x) - ref) / std::abs(ref));
  }
  return worst;
}

}  // namespace

TEST_CASE("fast_sin / fast_cos hold their ~3e-7 bound over [-pi, pi]") {
  auto const se{max_abs_error(
    -std::numbers::pi,
    std::numbers::pi,
    200000,
    [](double x) { return math::fast_sin(math::radians_d{x}); },
    [](double x) { return std::sin(x); }
  )};
  auto const ce{max_abs_error(
    -std::numbers::pi,
    std::numbers::pi,
    200000,
    [](double x) { return math::fast_cos(math::radians_d{x}); },
    [](double x) { return std::cos(x); }
  )};
  CHECK(se < 3.5e-7);  // measured ~3.12e-7, NOT the formerly-claimed 1e-7
  CHECK(ce < 3.5e-7);
}

TEST_CASE("lut_sin / lut_cos hold their ~4.7e-6 bound") {
  auto const se{max_abs_error(
    -std::numbers::pi,
    std::numbers::pi,
    200000,
    [](double x) { return math::lut_sin(math::radians_d{x}); },
    [](double x) { return std::sin(x); }
  )};
  auto const ce{max_abs_error(
    -std::numbers::pi,
    std::numbers::pi,
    200000,
    [](double x) { return math::lut_cos(math::radians_d{x}); },
    [](double x) { return std::cos(x); }
  )};
  // The analytic linear-interpolation bound is (1/8)(2pi/1024)^2 = 4.71e-6.
  CHECK(se < 5.0e-6);  // measured ~4.71e-6, NOT the formerly-claimed 3e-6
  CHECK(ce < 5.0e-6);
  CHECK(se > 3.0e-6);  // and it genuinely exceeds the old 3e-6 claim
}

TEST_CASE("fast inverse trig hold their bounds") {
  auto const asin_e{max_abs_error(
    -0.999,
    0.999,
    100000,
    [](double x) { return math::fast_asin(x).value(); },
    [](double x) { return std::asin(x); }
  )};
  auto const atan_e{max_abs_error(
    -50.0,
    50.0,
    100000,
    [](double x) { return math::fast_atan(x).value(); },
    [](double x) { return std::atan(x); }
  )};
  CHECK(asin_e < 1.0e-7);  // doc says ~5e-8 (away from the endpoints)
  CHECK(atan_e < 7.0e-6);  // doc says ~6e-6
}

TEST_CASE("power approximations hold their bounds") {
  auto const isqrt_e{max_rel_error(
    0.01,
    1000.0,
    100000,
    [](double x) { return math::fast_inv_sqrt(x); },
    [](double x) { return 1.0 / std::sqrt(x); }
  )};
  auto const exp_e{max_rel_error(
    -10.0,
    10.0,
    100000,
    [](double x) { return math::fast_exp(x); },
    [](double x) { return std::exp(x); }
  )};
  auto const log_e{max_abs_error(
    0.01,
    1000.0,
    100000,
    [](double x) { return math::fast_log(x); },
    [](double x) { return std::log(x); }
  )};
  CHECK(isqrt_e < 6.0e-6);  // doc ~5e-6
  CHECK(exp_e < 4.0e-6);    // doc ~3e-6
  CHECK(log_e < 1.5e-7);    // doc ~1e-7 (measured sits right at the limit)
}

TEST_CASE("lerp endpoint behaviour matches the documented contract") {
  // t=0 is exact; a normal-magnitude t=1 is exact too.
  CHECK(math::lerp(3.0, 9.0, 0.0) == 3.0);
  CHECK(math::lerp(3.0, 9.0, 1.0) == 9.0);
  // But the fast form is NOT endpoint-exact at t=1 under large cancellation, as
  // the doc now warns: lhs + 1*(rhs-lhs) loses rhs when |lhs| >> |rhs|.
  CHECK(math::lerp(1e20, 1.0, 1.0) != 1.0);
}

TEST_CASE("angle_diff respects its [-pi, pi) half-open range") {
  // A difference of exactly pi maps to -pi (the half-open low end), which would
  // be excluded by the formerly-claimed (-pi, pi].
  auto const d{math::angle_diff(math::radians_d{std::numbers::pi}, math::radians_d{0.0}).value()};
  CHECK(d == doctest::Approx(-std::numbers::pi));
  CHECK(d < 0.0);  // it is -pi, not +pi
}
