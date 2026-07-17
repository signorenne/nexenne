/**
 * @file
 * @brief Tests for the EPA contact manifold (epa.hpp contact_manifold): a
 *        multi-point face contact, and the single-point fallback for curves.
 */

#include <doctest/doctest.h>

#include <array>
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
using vec3 = nm::vector3_f;

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

TEST_CASE("contact_manifold: a box-box face contact yields a contact polygon") {
  // Two unit cubes overlapping a little on x: their facing 1x1 faces are aligned,
  // so the manifold is the full overlap square, not a single point.
  auto const va{cube_vertices(vec3{0, 0, 0}, 0.5f)};
  auto const vb{cube_vertices(vec3{0.8f, 0, 0}, 0.5f)};
  geo::convex_hull3_f const a{std::span<vec3 const>{va}};
  geo::convex_hull3_f const b{std::span<vec3 const>{vb}};

  auto const hit{geo::gjk(a, b, vec3{1, 0, 0})};
  REQUIRE(hit.overlap);
  auto const contact{geo::epa(a, b, hit.simplex)};
  REQUIRE(contact.converged);

  auto const m{geo::contact_manifold(a, b, contact)};
  CHECK(m.count >= 4);  // a face contact, not a single point.
  CHECK(nm::abs(m.normal.x()) == doctest::Approx(1.0).epsilon(0.05));

  // Every manifold point lies on one contact plane (constant x here) and the set
  // spans the 1x1 overlap square in y and z.
  auto min_y{1e9f}, max_y{-1e9f}, min_z{1e9f}, max_z{-1e9f};
  auto const plane_x{m.points[0].x()};
  for (auto i{std::size_t{0}}; i < m.count; ++i) {
    CHECK(m.points[i].x() == doctest::Approx(static_cast<double>(plane_x)).epsilon(1e-4));
    min_y = nm::min(min_y, m.points[i].y());
    max_y = nm::max(max_y, m.points[i].y());
    min_z = nm::min(min_z, m.points[i].z());
    max_z = nm::max(max_z, m.points[i].z());
  }
  CHECK(max_y - min_y == doctest::Approx(1.0).epsilon(0.05));
  CHECK(max_z - min_z == doctest::Approx(1.0).epsilon(0.05));
}

TEST_CASE("contact_manifold: a curved contact collapses to a single point") {
  // A sphere pressed into a box face: the box has a flat face but the sphere does
  // not, so the manifold is the single deepest point.
  auto const vb{cube_vertices(vec3{0, 0, 0}, 1.0f)};
  geo::convex_hull3_f const box{std::span<vec3 const>{vb}};
  geo::sphere3_f const ball{vec3{1.8f, 0, 0}, 1.0f};  // overlaps the +x face
  auto const hit{geo::gjk<float>(ball, box, vec3{-1, 0, 0})};
  REQUIRE(hit.overlap);
  auto const contact{geo::epa<float>(ball, box, hit.simplex)};
  REQUIRE(contact.converged);

  auto const m{geo::contact_manifold<float>(ball, box, contact)};
  CHECK(m.count == 1);  // the sphere has no flat face to clip against.
}

}  // namespace
