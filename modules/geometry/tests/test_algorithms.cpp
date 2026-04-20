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
#include <nexenne/geometry/gjk.hpp>
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

TEST_CASE("gjk: overlapping cubes report overlap and a terminal tetrahedron") {
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.5f, 0, 0}, 0.5f)};  // half overlap on x
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};
  auto const r{geo::gjk(a, b, vec3{1, 0, 0})};
  CHECK(r.overlap);
  CHECK(r.simplex.count == 4);  // overlap terminates on an enclosing tetrahedron
  CHECK(r.iterations >= 1);
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

}  // namespace
