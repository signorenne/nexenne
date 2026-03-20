#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <numbers>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/random.hpp>
#include <nexenne/math/vector_algorithms.hpp>
#include <nexenne/random/pcg.hpp>

namespace math = nexenne::math;
namespace rng = nexenne::random;

namespace {
// A fixed seed keeps these statistical tests deterministic and reproducible.
auto seeded() -> rng::pcg32 {
  return rng::pcg32{0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};
}

constexpr std::size_t samples{100000};
}  // namespace

TEST_CASE("unit_vector2 and unit_vector3 are always unit length") {
  auto g{seeded()};
  for (std::size_t i{0}; i < samples; ++i) {
    CHECK(math::length(math::unit_vector2<double>(g)) == doctest::Approx(1.0).epsilon(1e-9));
    CHECK(math::length(math::unit_vector3<double>(g)) == doctest::Approx(1.0).epsilon(1e-9));
  }
}

TEST_CASE("disc and ball samples stay strictly inside the unit radius") {
  auto g{seeded()};
  for (std::size_t i{0}; i < samples; ++i) {
    CHECK(math::length_squared(math::point_in_unit_disc<double>(g)) < 1.0);
    CHECK(math::length_squared(math::point_in_unit_ball<double>(g)) < 1.0);
  }
}

TEST_CASE("random_angle stays in [-pi, pi)") {
  auto g{seeded()};
  for (std::size_t i{0}; i < samples; ++i) {
    auto const a{math::random_angle<double>(g).value()};
    CHECK(a >= -math::pi);
    CHECK(a < math::pi);
  }
}

TEST_CASE("uniform directions and disc points have a near-zero mean (no bias)") {
  // For a uniform distribution the component means converge to 0; with 1e5
  // samples the standard error is ~3e-3, so a 0.02 tolerance is comfortable.
  auto g{seeded()};
  math::vector3_d dir_sum{0, 0, 0};
  math::vector2_d disc_sum{0, 0};
  for (std::size_t i{0}; i < samples; ++i) {
    dir_sum = dir_sum + math::unit_vector3<double>(g);
    disc_sum = disc_sum + math::point_in_unit_disc<double>(g);
  }
  auto const n{static_cast<double>(samples)};
  CHECK(std::abs(dir_sum.x() / n) < 0.02);
  CHECK(std::abs(dir_sum.y() / n) < 0.02);
  CHECK(std::abs(dir_sum.z() / n) < 0.02);
  CHECK(std::abs(disc_sum.x() / n) < 0.02);
  CHECK(std::abs(disc_sum.y() / n) < 0.02);
}

TEST_CASE("random_angle<float> stays strictly below pi (half-open, regression)") {
  // The double->float narrowing in uniform_real_in must not let the upper bound
  // be reached; random_angle<float> must stay in [-pi_f, pi_f).
  auto g{seeded()};
  for (std::size_t i{0}; i < 200000; ++i) {
    auto const a{math::random_angle<float>(g).value()};
    CHECK(a < std::numbers::pi_v<float>);
    CHECK(a >= -std::numbers::pi_v<float>);
  }
}
