// Dense-random validation for the two places a transcription typo in matrix.hpp
// would hide: the closed-form determinant/inverse cofactors, and the projection
// builders. The per-function suite only exercises sparse/identity/one hand-picked
// matrix and never checks the left inverse; this sweeps random well-conditioned
// matrices and asserts inv(A)*A == A*inv(A) == I for 2/3/4, then maps the frustum
// and box corners through the projections onto the clip cube.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

#include <nexenne/math/matrix.hpp>
#include <nexenne/math/projection.hpp>
#include <nexenne/math/vector.hpp>

namespace math = nexenne::math;

namespace {

// A small deterministic generator so the sweep is reproducible (no random dep).
class lcg {
public:
  explicit constexpr lcg(std::uint64_t seed) noexcept : m_state{seed} {}

  // A double in [-2, 2).
  auto next() noexcept -> double {
    m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
    auto const unit{static_cast<double>(m_state >> 11) * (1.0 / 9007199254740992.0)};
    return unit * 4.0 - 2.0;
  }

private:
  std::uint64_t m_state{};
};

template <std::size_t N>
auto near_identity(math::matrix<double, N> const& m, double tol) -> bool {
  for (std::size_t r{0}; r < N; ++r) {
    for (std::size_t c{0}; c < N; ++c) {
      double const expected{r == c ? 1.0 : 0.0};
      if (std::abs(m(r, c) - expected) > tol) {
        return false;
      }
    }
  }
  return true;
}

template <std::size_t N>
auto run_inverse_sweep(lcg& rng, int trials) -> int {
  int tested{0};
  for (int t{0}; t < trials; ++t) {
    math::matrix<double, N> a{};
    for (std::size_t r{0}; r < N; ++r) {
      for (std::size_t c{0}; c < N; ++c) {
        a(r, c) = rng.next();
      }
    }
    // Only test well-conditioned matrices, so roundoff stays tight.
    if (std::abs(math::determinant(a)) < 0.5) {
      continue;
    }
    auto const inv{math::inverse(a)};
    REQUIRE(inv.has_value());
    CHECK(near_identity(a * *inv, 1e-9));  // right inverse
    CHECK(near_identity(*inv * a, 1e-9));  // left inverse (never checked before)
    ++tested;
  }
  return tested;
}

}  // namespace

TEST_CASE("inverse round-trips both ways on dense random matrices") {
  lcg rng{0x9E3779B97F4A7C15ULL};
  CHECK(run_inverse_sweep<2>(rng, 4000) > 1000);
  CHECK(run_inverse_sweep<3>(rng, 4000) > 1000);
  CHECK(run_inverse_sweep<4>(rng, 4000) > 1000);
}

TEST_CASE("perspective maps all frustum corners onto the GL clip cube") {
  double const fovy{1.1};
  double const aspect{16.0 / 9.0};
  double const n{0.25};
  double const f{80.0};
  auto const p{math::perspective(fovy, aspect, n, f)};

  double const th{std::tan(fovy / 2.0)};
  // Eight frustum corners (x, y at the near and far planes), each must land on a
  // corner of the [-1, 1] cube after the perspective divide.
  for (double zsign : {n, f}) {
    double const half_h{zsign * th};
    double const half_w{half_h * aspect};
    for (double sx : {-1.0, 1.0}) {
      for (double sy : {-1.0, 1.0}) {
        auto const clip{p * math::vector4_d{sx * half_w, sy * half_h, -zsign, 1.0}};
        CHECK(clip.x() / clip.w() == doctest::Approx(sx));
        CHECK(clip.y() / clip.w() == doctest::Approx(sy));
        CHECK(clip.z() / clip.w() == doctest::Approx(zsign == n ? -1.0 : 1.0).epsilon(1e-9));
      }
    }
  }
}

TEST_CASE("ortho maps all box corners onto the GL clip cube") {
  auto const o{math::ortho(-3.0, 5.0, -2.0, 6.0, 0.5, 40.0)};
  // The box corners (left/right, bottom/top, near/far) map to the cube corners.
  for (double x : {-3.0, 5.0}) {
    for (double y : {-2.0, 6.0}) {
      for (double z : {0.5, 40.0}) {
        auto const clip{o * math::vector4_d{x, y, -z, 1.0}};
        CHECK(clip.x() == doctest::Approx(x == -3.0 ? -1.0 : 1.0));
        CHECK(clip.y() == doctest::Approx(y == -2.0 ? -1.0 : 1.0));
        CHECK(clip.z() == doctest::Approx(z == 0.5 ? -1.0 : 1.0));
        CHECK(clip.w() == doctest::Approx(1.0));  // no perspective divide
      }
    }
  }
}
