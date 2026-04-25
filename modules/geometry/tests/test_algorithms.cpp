/**
 * @file
 * @brief Tests for the nexenne::geometry algorithm layer (Phase 4): the
 *        convex_hull3 support shape and the GJK/EPA collision algorithms.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
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

// A unit cube centered at the origin, as eight corner vertices. Owned by the
// caller; the hull is a non-owning view over it.
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

  // Along +X the support is a corner with x == +0.5; the other components are
  // whichever corner the scan settles on, but x must be the maximum.
  CHECK(cube.support(vec3{1, 0, 0}).x() == doctest::Approx(0.5f));
  CHECK(cube.support(vec3{-1, 0, 0}).x() == doctest::Approx(-0.5f));
  CHECK(cube.support(vec3{0, 1, 0}).y() == doctest::Approx(0.5f));
  CHECK(cube.support(vec3{0, 0, -1}).z() == doctest::Approx(-0.5f));

  // A diagonal direction selects the single corner extreme on every axis.
  auto const corner{cube.support(vec3{1, 1, 1})};
  CHECK(corner.x() == doctest::Approx(0.5f));
  CHECK(corner.y() == doctest::Approx(0.5f));
  CHECK(corner.z() == doctest::Approx(0.5f));
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
  // The whole surface is usable in constant evaluation: view a constexpr vertex
  // array, query the support point, and bound the vertices, all at compile time.
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

// Eight corner vertices of an axis-aligned cube centered at c with half-extent h.
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
  // Seed with a deliberately unhelpful direction; GJK must still separate them.
  CHECK_FALSE(geo::gjk(a, b, vec3{1, 0, 0}).overlap);
}

TEST_CASE("gjk: touching cubes report overlap") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{1, 0, 0}, 0.5f)};  // faces meet at x == 0.5
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  CHECK(geo::gjk(a, b, vec3{1, 0, 0}).overlap);
}

TEST_CASE("gjk: overlapping cubes report overlap and a usable terminal simplex") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.5f, 0, 0}, 0.5f)};  // half overlap on x
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const r{geo::gjk(a, b, vec3{1, 0, 0})};
  CHECK(r.overlap);
  // The signed-volumes GJK reports overlap with whatever simplex carries the
  // origin (the origin can lie on an edge or face), which EPA grows to a
  // tetrahedron; the terminal simplex just has to be a valid carrier.
  CHECK(r.simplex.count >= 1);
  CHECK(r.simplex.count <= 4);
  CHECK(r.iterations >= 1);
  // The seed must still be expandable into a converged EPA result.
  auto const e{geo::epa(a, b, r.simplex)};
  CHECK(e.converged);
}

TEST_CASE("gjk: deep concentric overlap is found regardless of seed direction") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 1.0f)};
  auto const vb{cube_vertices(vec3{0, 0, 0}, 0.25f)};  // b sits well inside a
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
  auto const r{geo::gjk(a, b, vec3{1, 0, 0})};  // deduces Real = float
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
  // Two equal axis-aligned cubes overlap exactly when their centers are within
  // 2h on every axis (interval intersection per axis). That analytic verdict is
  // the ground truth GJK must reproduce. We skip a thin band around the exact
  // boundary, where a touching contact is floating-point ambiguous, and check
  // every unambiguous case. Hundreds of random placements exercise the simplex
  // line/triangle/tetrahedron reductions far past the handful of fixed cases.
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
      continue;  // too close to the exact touching boundary to classify
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
  CHECK(checked > 600);  // the skip band should never swallow most of the trials
}

TEST_CASE("epa: penetration depth and a unit normal on an axis-aligned overlap") {
  // Cubes of size 1 (half 0.5) centered at -0.25 and +0.25 overlap by 0.5 on x
  // and fully on y and z, so the minimum separation is 0.5 along x.
  auto const va{cube_vertices(vec3{-0.25f, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.25f, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const g{geo::gjk(a, b, vec3{1, 0, 0})};
  REQUIRE(g.overlap);
  auto const e{geo::epa(a, b, g.simplex)};
  REQUIRE(e.converged);
  CHECK(e.penetration_depth == doctest::Approx(0.5f).epsilon(0.05f));
  CHECK(std::abs(e.normal.x()) > 0.9f);  // separation runs along x
  CHECK(std::abs(e.normal.y()) < 0.1f);
  CHECK(std::abs(e.normal.z()) < 0.1f);
  CHECK(nm::length(e.normal) == doctest::Approx(1.0f));  // normal is unit length
}

TEST_CASE("epa: moving B out by depth*normal separates the shapes") {
  // A convention-independent check of normal AND depth AND sign together:
  // translating B along the penetration vector must end the overlap.
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
  // Equal cubes (size 1) offset along x by `off` overlap by 1 - off on x.
  for (auto const off : {0.2f, 0.4f, 0.6f, 0.8f}) {
    auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
    auto const vb{cube_vertices(vec3{off, 0, 0}, 0.5f)};
    geo::convex_hull3_f const a{std::span<vec3 const>{va}};
    geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
    auto const g{geo::gjk(a, b, vec3{1, 0, 0})};
    REQUIRE(g.overlap);
    auto const e{geo::epa(a, b, g.simplex)};
    REQUIRE(e.converged);
    CHECK(e.penetration_depth == doctest::Approx(1.0f - off).epsilon(0.05f));
  }
}

TEST_CASE("epa: the normal is the B push-out direction (out of A toward B)") {
  // Convention lock: with A at the origin and B to its +x, the normal must point
  // along +x, so B + depth*normal moves B away from A. Guards the documented
  // direction against a silent sign flip.
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.6f, 0, 0}, 0.5f)};  // B is to the +x of A
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const g{geo::gjk(a, b, vec3{1, 0, 0})};
  REQUIRE(g.overlap);
  auto const e{geo::epa(a, b, g.simplex)};
  REQUIRE(e.converged);
  CHECK(e.normal.x() == doctest::Approx(1.0f).epsilon(1e-3));  // points A -> B (+x).
}

TEST_CASE("epa: a degenerate seed simplex does not converge") {
  // A lower-dimensional simplex is normally grown to a tetrahedron, but a
  // degenerate one (here two coincident origin vertices) cannot be expanded, so
  // EPA reports non-convergence rather than inventing a result.
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::gjk_simplex3<float> partial{};
  partial.count = 2;  // two coincident default (zero) vertices: not expandable.
  auto const e{geo::epa(a, a, partial)};
  CHECK_FALSE(e.converged);
}

TEST_CASE("epa: differential against exact box penetration over random overlaps") {
  // For two equal axis-aligned cubes (size 1) the per-axis overlap is 1 - |off|,
  // and the true penetration is the smallest of the three (the minimum
  // translation axis). EPA must reproduce that depth, and translating B out
  // along its result must separate the pair. We skip near-ties where two axes
  // share the minimum, since the penetration axis is then ambiguous.
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
      continue;  // two axes tie for the minimum: ambiguous penetration axis
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
    CHECK(e.penetration_depth == doctest::Approx(expected_depth).epsilon(0.02f));
    CHECK(nm::length(e.normal) == doctest::Approx(1.0f).epsilon(0.01f));

    // Joint check: pushing B out by the penetration vector ends the overlap.
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
  // Spheres need support overloads; the signed-volumes GJK reports the distance
  // and the closest point on each surface for a separated pair.
  geo::sphere3_f const a{vec3{0, 0, 0}, 1.0f};
  geo::sphere3_f const b{vec3{5, 0, 0}, 2.0f};
  auto const r{geo::gjk<float>(a, b, vec3{1, 0, 0})};
  CHECK_FALSE(r.overlap);
  CHECK(r.distance == doctest::Approx(5.0f - 1.0f - 2.0f).epsilon(1e-4));  // |c| - rA - rB.
  // Closest points lie on each surface, on the line of centers.
  CHECK(r.closest_a.x() == doctest::Approx(1.0f).epsilon(1e-3));
  CHECK(r.closest_b.x() == doctest::Approx(3.0f).epsilon(1e-3));
  CHECK(nm::length(r.closest_b - r.closest_a) == doctest::Approx(r.distance).epsilon(1e-3));
}

TEST_CASE("gjk: separation distance of two boxes matches the axis gap") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};  // x in [-0.5, 0.5]
  auto const vb{cube_vertices(vec3{3, 0, 0}, 0.5f)};  // x in [2.5, 3.5]
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const r{geo::gjk(a, b, vec3{1, 0, 0})};
  CHECK_FALSE(r.overlap);
  CHECK(r.distance == doctest::Approx(2.0f).epsilon(1e-4));  // gap between 0.5 and 2.5.
}

TEST_CASE("gjk: distance is found regardless of the seed direction") {
  geo::sphere3_f const a{vec3{0, 0, 0}, 1.0f};
  geo::sphere3_f const b{vec3{0, 4, 0}, 1.0f};
  for (auto const& seed : {vec3{1, 0, 0}, vec3{0, 1, 0}, vec3{-1, -1, -1}, vec3{0, 0, 1}}) {
    auto const r{geo::gjk<float>(a, b, seed)};
    CHECK_FALSE(r.overlap);
    CHECK(r.distance == doctest::Approx(2.0f).epsilon(1e-3));  // 4 - 1 - 1.
  }
}

}  // namespace
