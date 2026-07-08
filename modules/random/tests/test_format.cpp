/**
 * @file
 * @brief Tests for the nexenne::random formatters (M4).
 */

#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>

#include <nexenne/random/format.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace rnd = nexenne::random;

TEST_CASE("engine to_string names the engine and prints its hex state") {
  rnd::pcg32 const g{42, 54};
  auto const text{rnd::to_string(g)};
  CHECK(text.starts_with("pcg32(state=0x"));
  CHECK(text.ends_with(")"));

  rnd::xoshiro256ss const x{123};
  auto const xtext{rnd::to_string(x)};
  CHECK(xtext.starts_with("xoshiro256ss(state=[0x"));
  CHECK(xtext.ends_with("])"));
}

TEST_CASE("distribution to_string names the distribution and lists its parameters") {
  CHECK(rnd::to_string(rnd::normal_distribution<double>{1.5, 2.0})
        == "normal_distribution(mean=1.5, stddev=2)");
  CHECK(rnd::to_string(rnd::exponential_distribution<double>{2.0})
        == "exponential_distribution(rate=2)");
  CHECK(rnd::to_string(rnd::gamma_distribution<double>{2.0, 3.0})
        == "gamma_distribution(shape=2, scale=3)");
  CHECK(rnd::to_string(rnd::poisson_distribution<>{4.0}) == "poisson_distribution(lambda=4)");
  CHECK(rnd::to_string(rnd::discrete_distribution<double>{{1.0, 3.0}})
        == "discrete_distribution([0.25, 0.75])");
}

TEST_CASE("std::format and operator<< agree with to_string") {
  rnd::normal_distribution<double> const dist{0.0, 1.0};
  CHECK(std::format("{}", dist) == rnd::to_string(dist));

  std::ostringstream os;
  os << dist;
  CHECK(os.str() == rnd::to_string(dist));

  rnd::pcg32 const g{7, 1};
  CHECK(std::format("{}", g) == rnd::to_string(g));

  rnd::xoshiro256ss const x{99};
  std::ostringstream xos;
  xos << x;
  CHECK(xos.str() == rnd::to_string(x));
}

TEST_CASE("distribution formatters forward the spec to each parameter") {
  CHECK(std::format("{:.2f}", rnd::normal_distribution<double>{1.0, 2.0})
        == "normal_distribution(mean=1.00, stddev=2.00)");
  CHECK(std::format("{:.1f}", rnd::gamma_distribution<double>{2.0, 3.0})
        == "gamma_distribution(shape=2.0, scale=3.0)");
  CHECK(std::format("{:.3f}", rnd::discrete_distribution<double>{{1.0, 3.0}})
        == "discrete_distribution([0.250, 0.750])");
}

TEST_CASE("an invalid parameter spec is rejected by the component formatter") {
  rnd::exponential_distribution<double> const dist{1.0};
  CHECK_THROWS_AS(
    nexenne::utility::discard(std::vformat("{:Z}", std::make_format_args(dist))), std::format_error
  );
}

}  // namespace
