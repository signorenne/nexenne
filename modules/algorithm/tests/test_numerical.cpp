/**
 * @file
 * @brief Tests for nexenne::algorithm numerical routines.
 *
 * Compensated sums are checked on catastrophic-cancellation inputs; root
 * finders against closed-form roots and their error paths; quadrature against
 * known integrals and Gauss-Legendre's degree-9 exactness; interpolation for
 * the knot-passing property; ODE steppers for convergence and energy
 * conservation; online stats against two-pass references with merge; and the
 * FFT by round-trip, a naive-DFT differential, and known spectra.
 */

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
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

namespace {

namespace alg = nexenne::algorithm;
using alg::numerical_error;
using cd = std::complex<double>;

[[nodiscard]] auto close(double const a, double const b, double const tol = 1e-9) -> bool {
  return std::abs(a - b) <= tol;
}

struct lcg {
  std::uint64_t state{0x243F6A8885A308D3ull};

  auto unit() -> double {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
  }
};

// kahan_sum / neumaier_sum

static_assert(alg::kahan_sum(std::array<double, 4>{1.0, 2.0, 3.0, 4.0}) == 10.0);

TEST_CASE("nexenne::algorithm compensated sums beat naive on cancellation") {
  // Naive summation of this loses the small terms entirely; the true sum is 2.
  auto const data{std::array<double, 4>{1.0, 1e100, 1.0, -1e100}};
  CHECK(close(alg::neumaier_sum(data), 2.0));

  // Many small additions: compensated stays far closer to the true value.
  auto vals{std::vector<double>(100000, 0.1)};
  CHECK(close(alg::kahan_sum(vals), 10000.0, 1e-6));
  CHECK(close(alg::neumaier_sum(vals), 10000.0, 1e-6));

  CHECK(alg::kahan_sum(std::vector<double>{}) == 0.0);                   // empty
  CHECK(alg::kahan_sum(std::array<double, 2>{1.0, 2.0}, 10.0) == 13.0);  // init
}

// bisection / newton

TEST_CASE("nexenne::algorithm::bisection finds roots and reports bad brackets") {
  auto const f{[](double x) { return x * x - 2.0; }};
  auto const r{alg::bisection<double>(f, 0.0, 2.0, 1e-12)};
  REQUIRE(r.has_value());
  CHECK(close(*r, std::numbers::sqrt2, 1e-9));

  // f(lo) and f(hi) share a sign: not bracketed.
  CHECK(alg::bisection<double>(f, 3.0, 4.0).error() == numerical_error::not_bracketed);
  // A function exactly zero at the lower endpoint returns it immediately.
  auto const g{[](double x) { return x; }};
  auto const at_end{alg::bisection<double>(g, 0.0, 5.0)};
  REQUIRE(at_end.has_value());
  CHECK(close(*at_end, 0.0));
}

TEST_CASE("nexenne::algorithm::newton converges quadratically and detects failure") {
  auto const f{[](double x) { return x * x - 2.0; }};
  auto const df{[](double x) { return 2.0 * x; }};
  auto const r{alg::newton<double>(f, df, 1.0, 1e-12)};
  REQUIRE(r.has_value());
  CHECK(close(*r, std::numbers::sqrt2, 1e-9));

  // Zero derivative encountered: no convergence.
  auto const flat{[](double) { return 1.0; }};
  auto const zero_d{[](double) { return 0.0; }};
  CHECK(alg::newton<double>(flat, zero_d, 1.0).error() == numerical_error::no_convergence);
}

// integration

TEST_CASE("nexenne::algorithm quadrature matches known integrals") {
  auto const sq{[](double x) { return x * x; }};          // integral 0..1 = 1/3
  auto const sine{[](double x) { return std::sin(x); }};  // integral 0..pi = 2
  CHECK(close(alg::trapezoidal(sq, 0.0, 1.0, 10000), 1.0 / 3.0, 1e-7));
  CHECK(close(alg::simpson(sq, 0.0, 1.0, 100), 1.0 / 3.0, 1e-10));  // exact for cubics
  CHECK(close(alg::simpson(sine, 0.0, std::numbers::pi, 1000), 2.0, 1e-9));
  CHECK(alg::trapezoidal(sq, 0.0, 1.0, 0) == 0.0);  // n == 0
}

TEST_CASE("nexenne::algorithm::gauss_legendre_5 is exact to degree 9") {
  // A degree-9 polynomial; 5-point Gauss-Legendre integrates it exactly.
  auto const p{[](double x) { return 3.0 * std::pow(x, 9) - 2.0 * std::pow(x, 4) + x - 5.0; }};
  // Antiderivative at the limits, evaluated by hand over [-1, 2].
  auto const exact{[](double x) {
    return 0.3 * std::pow(x, 10) - 0.4 * std::pow(x, 5) + 0.5 * x * x - 5.0 * x;
  }};
  CHECK(close(alg::gauss_legendre_5(p, -1.0, 2.0), exact(2.0) - exact(-1.0), 1e-9));
}

// interpolation

TEST_CASE("nexenne::algorithm stateless interpolation") {
  CHECK(close(alg::lerp(0.0, 10.0, 0.5), 5.0));
  CHECK(close(alg::smoothstep(0.0, 1.0, 0.0), 0.0));
  CHECK(close(alg::smoothstep(0.0, 1.0, 1.0), 1.0));
  CHECK(close(alg::smoothstep(0.0, 1.0, 0.5), 0.5));
  CHECK(close(alg::smootherstep(0.0, 1.0, 0.5), 0.5));
  CHECK(close(alg::bilinear(0.0, 1.0, 1.0, 2.0, 0.5, 0.5), 1.0));
  CHECK(close(alg::catmull_rom(0.0, 1.0, 2.0, 3.0, 0.0), 1.0));  // hits p1
  CHECK(close(alg::catmull_rom(0.0, 1.0, 2.0, 3.0, 1.0), 2.0));  // hits p2
}

TEST_CASE("nexenne::algorithm interpolators pass through their knots") {
  auto const xs{std::array<double, 4>{0.0, 1.0, 2.0, 3.0}};
  auto const ys{std::array<double, 4>{0.0, 1.0, 4.0, 9.0}};  // y = x^2 samples
  auto const lin{alg::linear_interpolator<double>{xs, ys}};
  auto const spl{alg::cubic_spline<double>{xs, ys}};
  for (auto i{std::size_t{0}}; i < xs.size(); ++i) {
    CHECK(close(lin(xs[i]), ys[i]));
    CHECK(close(spl(xs[i]), ys[i]));
  }
  CHECK(close(lin(0.5), 0.5));   // midpoint of first segment
  CHECK(close(lin(-1.0), 0.0));  // clamp low
  CHECK(close(lin(10.0), 9.0));  // clamp high
  CHECK(close(spl(-1.0), 0.0));  // clamp low
  CHECK(close(spl(10.0), 9.0));  // clamp high
}

// ode

TEST_CASE("nexenne::algorithm ODE steppers integrate dy/dt = y to e") {
  auto const f{[](double, double y) { return y; }};
  auto integrate{[&](auto stepper, std::size_t const steps) {
    auto const dt{1.0 / static_cast<double>(steps)};
    auto y{1.0};
    auto t{0.0};
    for (auto i{std::size_t{0}}; i < steps; ++i) {
      y = stepper(f, t, y, dt);
      t += dt;
    }
    return y;
  }};
  auto const euler{integrate(
    [](auto&& g, double t, double y, double dt) { return alg::euler_step<double>(g, t, y, dt); },
    100000
  )};
  auto const rk4{integrate(
    [](auto&& g, double t, double y, double dt) { return alg::rk4_step<double>(g, t, y, dt); }, 100
  )};
  CHECK(close(euler, std::numbers::e, 1e-3));  // first order: loose
  CHECK(close(rk4, std::numbers::e, 1e-9));    // fourth order: tight with few steps
}

TEST_CASE("nexenne::algorithm velocity Verlet conserves harmonic energy") {
  // a(x) = -x is the unit harmonic oscillator; energy 0.5*(v^2 + x^2) is fixed.
  auto const accel{[](double x) { return -x; }};
  auto x{1.0};
  auto v{0.0};
  auto const dt{0.001};
  auto const e0{0.5 * (v * v + x * x)};
  for (auto i{0}; i < 100000; ++i) {
    auto const [xn, vn]{alg::velocity_verlet_step<double>(accel, x, v, dt)};
    x = xn;
    v = vn;
  }
  CHECK(close(0.5 * (v * v + x * x), e0, 1e-4));  // symplectic: bounded drift
}

// online_stats

TEST_CASE("nexenne::algorithm::running_stats matches a two-pass reference and merges") {
  auto gen{lcg{}};
  auto data{std::vector<double>{}};
  auto rs{alg::running_stats<double>{}};
  for (auto i{0}; i < 1000; ++i) {
    auto const x{1.0e6 + gen.unit()};  // large mean, small spread: Welford shines
    data.push_back(x);
    rs.push(x);
  }
  auto mean_ref{0.0};
  for (auto const x : data) {
    mean_ref += x;
  }
  mean_ref /= static_cast<double>(data.size());
  auto var_ref{0.0};
  for (auto const x : data) {
    var_ref += (x - mean_ref) * (x - mean_ref);
  }
  var_ref /= static_cast<double>(data.size());
  CHECK(rs.count() == 1000);
  CHECK(close(rs.mean(), mean_ref, 1e-6));
  CHECK(close(rs.variance(), var_ref, 1e-6));
  CHECK(close(rs.stddev(), std::sqrt(var_ref), 1e-6));

  // merge(a, b) equals processing the union.
  auto a{alg::running_stats<double>{}};
  auto b{alg::running_stats<double>{}};
  for (auto i{std::size_t{0}}; i < data.size(); ++i) {
    (i < 400 ? a : b).push(data[i]);
  }
  a.merge(b);
  CHECK(a.count() == 1000);
  CHECK(close(a.mean(), rs.mean(), 1e-6));
  CHECK(close(a.variance(), rs.variance(), 1e-6));
}

TEST_CASE("nexenne::algorithm::histogram buckets, bounds, and quantile") {
  auto h{alg::histogram<double, 10>{0.0, 10.0}};
  for (auto i{0}; i < 10; ++i) {
    for (auto j{0}; j <= i; ++j) {
      h.push(static_cast<double>(i) + 0.5);  // bucket i gets i+1 samples
    }
  }
  h.push(-1.0);            // underflow
  h.push(100.0);           // overflow
  CHECK(h.total() == 57);  // 55 in-range + 2 out
  CHECK(h.underflow() == 1);
  CHECK(h.overflow() == 1);
  CHECK(h.bucket(0) == 1);
  CHECK(h.bucket(9) == 10);
  CHECK(h.quantile(0.0) >= 0.0);
  CHECK(h.quantile(1.0) <= 10.0);
  h.reset();
  CHECK(h.total() == 0);
}

TEST_CASE("nexenne::algorithm::ema_stats tracks recent values") {
  auto e{alg::ema_stats<double>{0.5}};
  CHECK(close(e.mean(), 0.0));  // unprimed
  e.push(10.0);
  CHECK(close(e.mean(), 10.0));  // first sample primes
  CHECK(close(e.variance(), 0.0));
  for (auto i{0}; i < 100; ++i) {
    e.push(20.0);
  }
  CHECK(e.mean() > 19.0);  // converges toward the recent level
  CHECK(e.variance() >= 0.0);
}

// fft

[[nodiscard]] auto naive_dft(std::span<cd const> const in) -> std::vector<cd> {
  auto const n{in.size()};
  auto out{std::vector<cd>(n)};
  for (auto k{std::size_t{0}}; k < n; ++k) {
    auto s{cd{0.0, 0.0}};
    for (auto t{std::size_t{0}}; t < n; ++t) {
      auto const angle{
        -2.0 * std::numbers::pi * static_cast<double>(k) * static_cast<double>(t)
        / static_cast<double>(n)
      };
      s += in[t] * cd{std::cos(angle), std::sin(angle)};
    }
    out[k] = s;
  }
  return out;
}

TEST_CASE("nexenne::algorithm::fft matches the naive DFT and round-trips") {
  auto gen{lcg{}};
  for (auto const n :
       {std::size_t{1},
        std::size_t{2},
        std::size_t{4},
        std::size_t{8},
        std::size_t{16},
        std::size_t{64}}) {
    auto signal{std::vector<cd>(n)};
    for (auto& x : signal) {
      x = cd{gen.unit() * 2.0 - 1.0, gen.unit() * 2.0 - 1.0};
    }
    auto const original{signal};
    auto const reference{naive_dft(std::span<cd const>{signal})};

    auto spectrum{signal};
    REQUIRE(alg::fft<double>(std::span<cd>{spectrum}).has_value());
    for (auto k{std::size_t{0}}; k < n; ++k) {
      CHECK(close(spectrum[k].real(), reference[k].real(), 1e-9));
      CHECK(close(spectrum[k].imag(), reference[k].imag(), 1e-9));
    }
    REQUIRE(alg::ifft<double>(std::span<cd>{spectrum}).has_value());
    for (auto k{std::size_t{0}}; k < n; ++k) {
      CHECK(close(spectrum[k].real(), original[k].real(), 1e-9));
      CHECK(close(spectrum[k].imag(), original[k].imag(), 1e-9));
    }
  }
}

TEST_CASE("nexenne::algorithm::fft known spectra and invalid size") {
  // DC signal: all energy in bin 0.
  auto dc{std::vector<cd>(8, cd{1.0, 0.0})};
  REQUIRE(alg::fft<double>(std::span<cd>{dc}).has_value());
  CHECK(close(dc[0].real(), 8.0));
  for (auto k{std::size_t{1}}; k < 8; ++k) {
    CHECK(close(std::abs(dc[k]), 0.0, 1e-9));
  }
  // Non-power-of-two is rejected.
  auto bad{std::vector<cd>(6)};
  CHECK(alg::fft<double>(std::span<cd>{bad}).error() == numerical_error::invalid_size);
  // Empty is a no-op success.
  auto empty{std::vector<cd>{}};
  CHECK(alg::fft<double>(std::span<cd>{empty}).has_value());
}

TEST_CASE("nexenne::algorithm::rfft matches the first half of the full FFT") {
  auto gen{lcg{}};
  auto const n{std::size_t{16}};
  auto real{std::vector<double>(n)};
  for (auto& x : real) {
    x = gen.unit() * 2.0 - 1.0;
  }
  auto full{std::vector<cd>(n)};
  for (auto i{std::size_t{0}}; i < n; ++i) {
    full[i] = cd{real[i], 0.0};
  }
  REQUIRE(alg::fft<double>(std::span<cd>{full}).has_value());

  auto const half{alg::rfft<double>(std::span<double const>{real})};
  REQUIRE(half.has_value());
  REQUIRE(half->size() == n / 2 + 1);
  for (auto k{std::size_t{0}}; k <= n / 2; ++k) {
    CHECK(close((*half)[k].real(), full[k].real(), 1e-9));
    CHECK(close((*half)[k].imag(), full[k].imag(), 1e-9));
  }
}

// degenerate / edge inputs

TEST_CASE("nexenne::algorithm interpolators degrade gracefully on tiny tables") {
  // Empty table yields zero; a single knot clamps to it everywhere.
  auto const none{std::span<double const>{}};
  CHECK(close(alg::linear_interpolator<double>{none, none}(3.0), 0.0));
  CHECK(close(alg::cubic_spline<double>{none, none}(3.0), 0.0));

  auto const x1{std::array<double, 1>{5.0}};
  auto const y1{std::array<double, 1>{9.0}};
  CHECK(close(alg::linear_interpolator<double>{x1, y1}(-100.0), 9.0));
  CHECK(close(alg::linear_interpolator<double>{x1, y1}(100.0), 9.0));
  CHECK(close(alg::cubic_spline<double>{x1, y1}(0.0), 9.0));

  // Two knots: the spline falls back to a straight line.
  auto const x2{std::array<double, 2>{0.0, 2.0}};
  auto const y2{std::array<double, 2>{0.0, 10.0}};
  CHECK(close(alg::cubic_spline<double>{x2, y2}(1.0), 5.0));
}

TEST_CASE("nexenne::algorithm::running_stats empty, single, reset, merge-with-empty") {
  auto rs{alg::running_stats<double>{}};
  CHECK(rs.count() == 0);
  CHECK(close(rs.mean(), 0.0));
  CHECK(close(rs.variance(), 0.0));
  CHECK(close(rs.min(), 0.0));
  CHECK(close(rs.max(), 0.0));
  CHECK(close(rs.range(), 0.0));

  rs.push(7.0);
  CHECK(rs.count() == 1);
  CHECK(close(rs.mean(), 7.0));
  CHECK(close(rs.variance(), 0.0));  // n < 2
  CHECK(close(rs.min(), 7.0));
  CHECK(close(rs.max(), 7.0));

  rs.push(11.0);
  rs.reset();
  CHECK(rs.count() == 0);

  // merge is a no-op with an empty accumulator on either side.
  auto a{alg::running_stats<double>{}};
  a.push(1.0);
  a.push(2.0);
  auto const before{a.mean()};
  a.merge(alg::running_stats<double>{});  // merging empty changes nothing
  CHECK(close(a.mean(), before));
  CHECK(a.count() == 2);
  auto fresh{alg::running_stats<double>{}};
  fresh.merge(a);  // merging into empty adopts the other
  CHECK(fresh.count() == 2);
  CHECK(close(fresh.mean(), before));
}

TEST_CASE("nexenne::algorithm::ema_stats unprimed and reset") {
  auto e{alg::ema_stats<double>{0.3}};
  CHECK(close(e.mean(), 0.0));  // unprimed
  CHECK(close(e.variance(), 0.0));
  e.push(5.0);
  e.push(6.0);
  e.reset();
  CHECK(close(e.mean(), 0.0));  // reset unprimes
  e.push(42.0);
  CHECK(close(e.mean(), 42.0));  // first post-reset sample primes again
}

TEST_CASE("nexenne::algorithm::histogram empty and boundary quantiles") {
  auto h{alg::histogram<double, 4>{0.0, 4.0}};
  CHECK(close(h.quantile(0.5), 0.0));  // empty histogram
  CHECK(h.total() == 0);
  h.push(0.5);
  h.push(3.5);
  CHECK(h.total() == 2);
  CHECK(h.quantile(0.0) >= 0.0);
  CHECK(h.quantile(1.0) <= 4.0);
  h.push(-5.0);  // underflow
  h.push(99.0);  // overflow
  CHECK(h.underflow() == 1);
  CHECK(h.overflow() == 1);
}

TEST_CASE("nexenne::algorithm root finders report non-convergence") {
  // A real root exists in the bracket, but one step cannot reach the tolerance.
  auto const f{[](double x) { return x * x - 2.0; }};
  CHECK(alg::bisection<double>(f, 0.0, 2.0, 1e-18, 2).error() == numerical_error::no_convergence);
  // Newton runs out of iterations chasing an unreachable tolerance.
  auto const df{[](double x) { return 2.0 * x; }};
  CHECK(alg::newton<double>(f, df, 1.0, 1e-18, 1).error() == numerical_error::no_convergence);
}

TEST_CASE("nexenne::algorithm compensated sums on tiny and signed inputs") {
  CHECK(close(alg::kahan_sum(std::array<double, 1>{42.0}), 42.0));
  CHECK(close(alg::neumaier_sum(std::array<double, 3>{-1.0, -2.0, -3.0}), -6.0));
  CHECK(close(alg::kahan_sum(std::array<double, 2>{1e-300, -1e-300}), 0.0));
}

}  // namespace
