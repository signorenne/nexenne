/**
 * @file
 * @brief Tests for the nexenne::geometry algorithm layer (Phase 4): the
 *        convex_hull3 support shape and the GJK/EPA collision algorithms.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <random>
#include <span>

#include <nexenne/geometry/convex_hull.hpp>
#include <nexenne/geometry/epa.hpp>
#include <nexenne/geometry/gjk.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/support.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec3 = nm::vector<float, 3>;

constexpr std::array<vec3, 8> unit_cube{
  vec3{-0.5f, -0.5f, -0.5f},
  vec3{0.5f, -0.5f, -0.5f},
  vec3{-0.5f, 0.5f, -0.5f},
  vec3{0.5f, 0.5f, -0.5f},
  vec3{-0.5f, -0.5f, 0.5f},
  vec3{0.5f, -0.5f, 0.5f},
  vec3{-0.5f, 0.5f, 0.5f},
  vec3{0.5f, 0.5f, 0.5f},
};

TEST_CASE("convex_hull3: support returns the furthest corner along a direction") {
  geo::convex_hull3_f const cube{std::span<vec3 const>{unit_cube}};

  CHECK(cube.support(vec3{1, 0, 0}).x() == doctest::Approx(0.5));
  CHECK(cube.support(vec3{-1, 0, 0}).x() == doctest::Approx(-0.5));
  CHECK(cube.support(vec3{0, 1, 0}).y() == doctest::Approx(0.5));
  CHECK(cube.support(vec3{0, 0, -1}).z() == doctest::Approx(-0.5));

  auto const corner{cube.support(vec3{1, 1, 1})};
  CHECK(corner.x() == doctest::Approx(0.5));
  CHECK(corner.y() == doctest::Approx(0.5));
  CHECK(corner.z() == doctest::Approx(0.5));
}

TEST_CASE("convex_hull3: the free support overload forwards to the member") {
  geo::convex_hull3_f const cube{std::span<vec3 const>{unit_cube}};
  auto const dir{vec3{1, 0, 0}};
  CHECK(support(cube, dir) == cube.support(dir));
}

TEST_CASE("convex_hull3: an empty hull supports to the origin") {
  geo::convex_hull3_f const empty{};
  CHECK(empty.vertices().empty());
  CHECK(empty.support(vec3{1, 2, 3}) == vec3{0, 0, 0});
}

TEST_CASE("convex_hull3: bounding_aabb tightly wraps the vertices") {
  geo::convex_hull3_f const cube{std::span<vec3 const>{unit_cube}};
  auto const box{geo::bounding_aabb(cube)};
  CHECK(box.min() == vec3{-0.5f, -0.5f, -0.5f});
  CHECK(box.max() == vec3{0.5f, 0.5f, 0.5f});

  geo::convex_hull3_f const empty{};
  CHECK(geo::empty(geo::bounding_aabb(empty)));
}

TEST_CASE("convex_hull3: support and bounding_aabb are constexpr") {
  constexpr auto checks{[] {
    geo::convex_hull3_f const cube{std::span<vec3 const>{unit_cube}};
    auto const s{cube.support(vec3{1, 1, 1})};
    auto const box{geo::bounding_aabb(cube)};
    return s.x() == 0.5f && s.y() == 0.5f && s.z() == 0.5f && box.min() == vec3{-0.5f, -0.5f, -0.5f}
           && box.max() == vec3{0.5f, 0.5f, 0.5f};
  }()};
  static_assert(checks, "convex_hull3 support/bounding_aabb must be constexpr");
  CHECK(checks);
}

constexpr auto cube_vertices(vec3 const c, float const h) -> std::array<vec3, 8> {
  return {
    c + vec3{-h, -h, -h},
    c + vec3{h, -h, -h},
    c + vec3{-h, h, -h},
    c + vec3{h, h, -h},
    c + vec3{-h, -h, h},
    c + vec3{h, -h, h},
    c + vec3{-h, h, h},
    c + vec3{h, h, h},
  };
}

TEST_CASE("gjk: separated cubes do not overlap") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{3, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  CHECK_FALSE(geo::gjk(a, b, vec3{1, 0, 0}).overlap);
}

TEST_CASE("gjk: a clearly separated diagonal pair does not overlap") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{2, 2, 2}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  CHECK_FALSE(geo::gjk(a, b, vec3{1, 0, 0}).overlap);
}

TEST_CASE("gjk: touching cubes report overlap") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{1, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  CHECK(geo::gjk(a, b, vec3{1, 0, 0}).overlap);
}

TEST_CASE("gjk: overlapping cubes report overlap and a usable terminal simplex") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.5f, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const r{geo::gjk(a, b, vec3{1, 0, 0})};
  CHECK(r.overlap);
  CHECK(r.simplex.count >= 1);
  CHECK(r.simplex.count <= 4);
  CHECK(r.iterations >= 1);
  auto const e{geo::epa(a, b, r.simplex)};
  CHECK(e.converged);
}

TEST_CASE("gjk: deep concentric overlap is found regardless of seed direction") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 1.0f)};
  auto const vb{cube_vertices(vec3{0, 0, 0}, 0.25f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  CHECK(geo::gjk(a, b, vec3{0, 1, 0}).overlap);
  CHECK(geo::gjk(a, b, vec3{0, 0, 1}).overlap);
}

TEST_CASE("gjk: Real is deduced from the direction (no explicit template arg)") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.5f, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const r{geo::gjk(a, b, vec3{1, 0, 0})};
  CHECK(r.overlap);
}

TEST_CASE("gjk: the overlap verdict is constexpr") {
  constexpr bool overlaps{[] {
    auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
    auto const vb{cube_vertices(vec3{0.5f, 0, 0}, 0.5f)};
    geo::convex_hull3_f const a{std::span<vec3 const>{va}};
    geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
    return geo::gjk(a, b, vec3{1, 0, 0}).overlap;
  }()};
  constexpr bool separated{[] {
    auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
    auto const vb{cube_vertices(vec3{3, 0, 0}, 0.5f)};
    geo::convex_hull3_f const a{std::span<vec3 const>{va}};
    geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
    return geo::gjk(a, b, vec3{1, 0, 0}).overlap;
  }()};
  static_assert(overlaps, "overlapping cubes must overlap at compile time");
  static_assert(!separated, "separated cubes must not overlap at compile time");
  CHECK(overlaps);
  CHECK_FALSE(separated);
}

TEST_CASE("gjk: differential against exact box overlap over random placements") {
  std::mt19937 rng{0xC0FFEEu};
  std::uniform_real_distribution<float> coord{-3.0f, 3.0f};
  auto const h{0.5f};
  auto const margin{1.0e-3f};
  auto checked{0};
  for (auto trial{0}; trial < 800; ++trial) {
    vec3 const ca{coord(rng), coord(rng), coord(rng)};
    vec3 const cb{coord(rng), coord(rng), coord(rng)};
    auto const gap{
      std::max({std::abs(ca.x() - cb.x()), std::abs(ca.y() - cb.y()), std::abs(ca.z() - cb.z())})
      - 2.0f * h
    };
    if (std::abs(gap) < margin) {
      continue;
    }
    auto const va{cube_vertices(ca, h)};
    auto const vb{cube_vertices(cb, h)};
    geo::convex_hull3_f const a{std::span<vec3 const>{va}};
    geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
    auto seed{cb - ca};
    if (nm::length_squared(seed) < 1.0e-12f) {
      seed = vec3{1, 0, 0};
    }
    auto const expected_overlap{gap < 0.0f};
    CHECK(geo::gjk(a, b, seed).overlap == expected_overlap);
    ++checked;
  }
  CHECK(checked > 600);
}

TEST_CASE("epa: penetration depth and a unit normal on an axis-aligned overlap") {
  auto const va{cube_vertices(vec3{-0.25f, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.25f, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const g{geo::gjk(a, b, vec3{1, 0, 0})};
  REQUIRE(g.overlap);
  auto const e{geo::epa(a, b, g.simplex)};
  REQUIRE(e.converged);
  CHECK(e.penetration_depth == doctest::Approx(0.5).epsilon(0.05));
  CHECK(std::abs(e.normal.x()) > 0.9f);
  CHECK(std::abs(e.normal.y()) < 0.1f);
  CHECK(std::abs(e.normal.z()) < 0.1f);
  CHECK(nm::length(e.normal) == doctest::Approx(1.0));
}

TEST_CASE("epa: moving B out by depth*normal separates the shapes") {
  auto const va{cube_vertices(vec3{-0.25f, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.25f, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const g{geo::gjk(a, b, vec3{1, 0, 0})};
  REQUIRE(g.overlap);
  auto const e{geo::epa(a, b, g.simplex)};
  REQUIRE(e.converged);

  auto vb_moved{vb};
  auto const push{e.normal * (e.penetration_depth + 0.05f)};
  for (auto& v : vb_moved) {
    v = v + push;
  }
  geo::convex_hull3_f const b_moved{std::span<vec3 const>{vb_moved}};
  CHECK_FALSE(geo::gjk(a, b_moved, vec3{1, 0, 0}).overlap);
}

TEST_CASE("epa: depth tracks the overlap amount across offsets") {
  for (auto const off : {0.2f, 0.4f, 0.6f, 0.8f}) {
    auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
    auto const vb{cube_vertices(vec3{off, 0, 0}, 0.5f)};
    geo::convex_hull3_f const a{std::span<vec3 const>{va}};
    geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
    auto const g{geo::gjk(a, b, vec3{1, 0, 0})};
    REQUIRE(g.overlap);
    auto const e{geo::epa(a, b, g.simplex)};
    REQUIRE(e.converged);
    CHECK(e.penetration_depth == doctest::Approx(1.0 - static_cast<double>(off)).epsilon(0.05));
  }
}

TEST_CASE("epa: the normal is the B push-out direction (out of A toward B)") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.6f, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const g{geo::gjk(a, b, vec3{1, 0, 0})};
  REQUIRE(g.overlap);
  auto const e{geo::epa(a, b, g.simplex)};
  REQUIRE(e.converged);
  CHECK(e.normal.x() == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("epa: a degenerate seed simplex does not converge") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::gjk_simplex3<float> partial{};
  partial.count = 2;
  auto const e{geo::epa(a, a, partial)};
  CHECK_FALSE(e.converged);
}

TEST_CASE("epa: differential against exact box penetration over random overlaps") {
  std::mt19937 rng{0x5EEDu};
  std::uniform_real_distribution<float> off{-0.9f, 0.9f};
  auto checked{0};
  for (auto trial{0}; trial < 500; ++trial) {
    vec3 const center{off(rng), off(rng), off(rng)};
    std::array<float, 3> overlap{
      1.0f - std::abs(center.x()), 1.0f - std::abs(center.y()), 1.0f - std::abs(center.z())
    };
    auto sorted{overlap};
    std::sort(sorted.begin(), sorted.end());
    if (sorted[1] - sorted[0] < 0.05f) {
      continue;
    }
    auto const expected_depth{sorted[0]};

    auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
    auto const vb{cube_vertices(center, 0.5f)};
    geo::convex_hull3_f const a{std::span<vec3 const>{va}};
    geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
    auto const g{geo::gjk(a, b, center)};
    REQUIRE(g.overlap);
    auto const e{geo::epa(a, b, g.simplex)};
    REQUIRE(e.converged);
    CHECK(
      e.penetration_depth == doctest::Approx(static_cast<double>(expected_depth)).epsilon(0.02)
    );
    CHECK(nm::length(e.normal) == doctest::Approx(1.0).epsilon(0.01));

    auto vb_moved{vb};
    auto const push{e.normal * (e.penetration_depth + 0.02f)};
    for (auto& v : vb_moved) {
      v = v + push;
    }
    geo::convex_hull3_f const b_moved{std::span<vec3 const>{vb_moved}};
    CHECK_FALSE(geo::gjk(a, b_moved, center).overlap);
    ++checked;
  }
  CHECK(checked > 350);
}

TEST_CASE("gjk: separation distance of two spheres matches the analytic value") {
  geo::sphere3_f const a{vec3{0, 0, 0}, 1.0f};
  geo::sphere3_f const b{vec3{5, 0, 0}, 2.0f};
  auto const r{geo::gjk<float>(a, b, vec3{1, 0, 0})};
  CHECK_FALSE(r.overlap);
  CHECK(r.distance == doctest::Approx(5.0 - 1.0 - 2.0).epsilon(1e-4));
  CHECK(r.closest_a.x() == doctest::Approx(1.0).epsilon(1e-3));
  CHECK(r.closest_b.x() == doctest::Approx(3.0).epsilon(1e-3));
  CHECK(
    nm::length(r.closest_b - r.closest_a)
    == doctest::Approx(static_cast<double>(r.distance)).epsilon(1e-3)
  );
}

TEST_CASE("gjk: separation distance of two boxes matches the axis gap") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{3, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const r{geo::gjk(a, b, vec3{1, 0, 0})};
  CHECK_FALSE(r.overlap);
  CHECK(r.distance == doctest::Approx(2.0).epsilon(1e-4));
}

TEST_CASE("gjk: distance is found regardless of the seed direction") {
  geo::sphere3_f const a{vec3{0, 0, 0}, 1.0f};
  geo::sphere3_f const b{vec3{0, 4, 0}, 1.0f};
  for (auto const& seed : {vec3{1, 0, 0}, vec3{0, 1, 0}, vec3{-1, -1, -1}, vec3{0, 0, 1}}) {
    auto const r{geo::gjk<float>(a, b, seed)};
    CHECK_FALSE(r.overlap);
    CHECK(r.distance == doctest::Approx(2.0).epsilon(1e-3));
  }
}

TEST_CASE("epa: two overlapping unit spheres converge with the default parameters") {
  geo::sphere3_f const a{vec3{0, 0, 0}, 1.0f};
  geo::sphere3_f const b{vec3{0.3f, 0, 0}, 1.0f};
  auto const g{geo::gjk<float>(a, b, vec3{1, 0, 0})};
  REQUIRE(g.overlap);
  auto const e{geo::epa<float>(a, b, g.simplex)};
  CHECK(e.converged);
  CHECK(e.penetration_depth == doctest::Approx(1.7).epsilon(0.02));
  CHECK(nm::length(e.normal) == doctest::Approx(1.0).epsilon(1e-3));
  CHECK(e.normal.x() > 0.9f);
}

TEST_CASE("epa: a non-converged result still reports the best face's contact points") {
  geo::sphere3_f const a{vec3{0, 0, 0}, 1.0f};
  geo::sphere3_f const b{vec3{0.3f, 0, 0}, 1.0f};
  auto const g{geo::gjk<float>(a, b, vec3{1, 0, 0})};
  REQUIRE(g.overlap);
  auto const e{geo::epa<float>(a, b, g.simplex, 4)};
  REQUIRE_FALSE(e.converged);
  CHECK(nm::length(e.contact_point_a) > 0.1f);
  CHECK(nm::length(e.contact_point_b) > 0.1f);
}

TEST_CASE("gjk: a too-small iteration cap does not misreport separated shapes as overlapping") {
  geo::sphere3_f const a{vec3{0, 0, 0}, 1.0f};
  geo::sphere3_f const b{vec3{2.5f, 0, 0}, 1.0f};
  CHECK_FALSE(geo::gjk<float>(a, b, vec3{0, 1, 0}, 1).overlap);
  auto const r{geo::gjk<float>(a, b, vec3{0, 1, 0})};
  CHECK_FALSE(r.overlap);
  CHECK(r.distance == doctest::Approx(0.5).epsilon(1e-3));
}

TEST_CASE("gjk: off-axis overlapping spheres report overlap") {
  geo::sphere3_d const a{nm::vector<double, 3>{0, 0, 0}, 1.0};
  geo::sphere3_d const b{nm::vector<double, 3>{1.5, 0.1, 0}, 1.0};
  auto const r{geo::gjk(a, b, b.center() - a.center())};
  CHECK(r.overlap);
  CHECK(r.distance == 0.0);
}

template <typename Real>
auto gjk_sphere_misclassified(Real const scale) -> int {
  using v3 = nm::vector<Real, 3>;
  auto rng{std::mt19937{42}};  // NOLINT(cert-msc32-c,cert-msc51-cpp)
  auto u{std::uniform_real_distribution<double>{-1.0, 1.0}};
  auto const coord{[&] { return static_cast<Real>(u(rng) * static_cast<double>(scale)); }};
  auto const radius{[&] {
    return static_cast<Real>((u(rng) + 1.2) * 0.3 * static_cast<double>(scale));
  }};
  auto wrong{0};
  for (auto i{0}; i < 2000; ++i) {
    auto const c1{v3{coord(), coord(), coord()}};
    auto const c2{v3{coord(), coord(), coord()}};
    auto const r1{radius()};
    auto const r2{radius()};
    auto const gap{
      std::sqrt(static_cast<double>(nm::length_squared(c2 - c1))) - static_cast<double>(r1)
      - static_cast<double>(r2)
    };
    if (std::abs(gap) <= 1e-3 * static_cast<double>(scale)) {
      continue;
    }
    auto const r{geo::gjk(geo::sphere3<Real>{c1, r1}, geo::sphere3<Real>{c2, r2}, c2 - c1)};
    if (r.overlap != (gap < 0.0)) {
      ++wrong;
    }
  }
  return wrong;
}

TEST_CASE("gjk: random sphere pairs are classified right in float and double") {
  for (auto const scale : {1e-2, 1.0, 1e4}) {
    CAPTURE(scale);
    CHECK(gjk_sphere_misclassified<double>(scale) == 0);
    CHECK(gjk_sphere_misclassified<float>(static_cast<float>(scale)) == 0);
  }
}

TEST_CASE("epa: a thin seed through the origin returns quickly with a sane result") {
  geo::sphere3_f const a{vec3{0x1.56b274p+5f, 0x1.0058a8p+1f, 0x1.cc2f3p+2f}, 0x1.9dfc72p+5f};
  geo::sphere3_f const b{vec3{0x1.8421bep+5f, 0x1.c6b442p+0f, 0x1.467bfp+4f}, 0x1.20a176p+6f};
  auto const vertex{[](vec3 const& on_a, vec3 const& on_b) {
    return geo::gjk_minkowski_point3<float>{on_a - on_b, on_a, on_b};
  }};
  auto seed{geo::gjk_simplex3<float>{}};
  seed.points = {
    vertex(
      vec3{0x1.07e1ecp+6f, 0x1.1d1d1p-1f, 0x1.aba8aep+5f},
      vec3{0x1.04259cp+4f, 0x1.e5691p+1f, -0x1.60dfe8p+5f}
    ),
    vertex(
      vec3{0x1.3b422p+4f, 0x1.b96a0cp+1f, -0x1.389ce2p+5f},
      vec3{0x1.431858p+6f, -0x1.eb4cfp-3f, 0x1.53adecp+6f}
    ),
    vertex(
      vec3{0x1.6682ep+4f, 0x1.68a594p+1f, -0x1.42c72ep+5f},
      vec3{0x1.340462p+6f, 0x1.47a9acp-1f, 0x1.5ac43ep+6f}
    ),
    vertex(
      vec3{0x1.fa2378p+5f, 0x1.301778p+0f, 0x1.b5d2fap+5f},
      vec3{0x1.40756cp+4f, 0x1.74c9d8p+1f, -0x1.6f0c8cp+5f}
    ),
  };
  seed.count = 4;
  auto const true_depth{
    static_cast<double>(a.radius() + b.radius() - nm::length(b.center() - a.center()))
  };
  for (auto const cap : {std::size_t{32}, std::size_t{64}}) {
    CAPTURE(cap);
    auto const start{std::chrono::steady_clock::now()};
    auto const e{geo::epa(a, b, seed, cap)};
    auto const elapsed{std::chrono::steady_clock::now() - start};
    CHECK(elapsed < std::chrono::milliseconds{50});
    CHECK(std::isfinite(e.penetration_depth));
    if (e.converged) {
      CHECK(std::abs(static_cast<double>(e.penetration_depth) - true_depth) < 0.05 * true_depth);
    }
  }
}

}  // namespace
