/**
 * @file
 * @brief Tests for the nexenne::geometry cross-type queries (Phase 3).
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <random>
#include <utility>

#include <nexenne/geometry/closest_point.hpp>
#include <nexenne/geometry/intersect.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using vec2 = nm::vector<double, 2>;
using vec3 = nm::vector<double, 3>;

[[nodiscard]] auto unit(vec3 const v) -> vec3 {
  return *nm::normalize(v);
}

TEST_CASE("closest_points: skew, parallel, and degenerate segments") {
  geo::segment3_d const s1{vec3{0, 0, 0}, vec3{2, 0, 0}};
  geo::segment3_d const s2{vec3{1, -1, 1}, vec3{1, 1, 1}};
  auto const [p1, p2]{geo::closest_points(s1, s2)};
  CHECK(p1 == vec3{1, 0, 0});
  CHECK(p2 == vec3{1, 0, 1});

  geo::segment3_d const pt{vec3{5, 3, 0}, vec3{5, 3, 0}};
  auto const [q1, q2]{geo::closest_points(pt, s1)};
  CHECK(q1 == vec3{5, 3, 0});
  CHECK(q2 == vec3{2, 0, 0});
}

TEST_CASE("ray vs plane / sphere / box / triangle") {
  auto const r{geo::ray3_d{vec3{0, 0, 0}, unit(vec3{0, 0, -1})}};

  auto const hp{geo::intersects(r, geo::plane3_d{vec3{0, 0, 1}, 5.0})};
  REQUIRE(hp.has_value());
  CHECK(*hp == doctest::Approx(5.0));

  auto const hs{geo::intersects(r, geo::sphere3_d{vec3{0, 0, -10}, 1.0})};
  REQUIRE(hs.has_value());
  CHECK(*hs == doctest::Approx(9.0));
  CHECK_FALSE(geo::intersects(r, geo::sphere3_d{vec3{5, 0, -10}, 1.0}));

  auto const hb{geo::intersects(r, geo::aabb3_d{vec3{-1, -1, -11}, vec3{1, 1, -9}})};
  REQUIRE(hb.has_value());
  CHECK(*hb == doctest::Approx(9.0));

  geo::triangle3_d const tri{vec3{-1, -1, -4}, vec3{1, -1, -4}, vec3{0, 2, -4}};
  auto const ht{geo::intersects(r, tri)};
  REQUIRE(ht.has_value());
  CHECK(*ht == doctest::Approx(4.0));
  CHECK_FALSE(geo::intersects(geo::ray3_d{vec3{0, 0, 0}, unit(vec3{0, 0, 1})}, tri));
}

TEST_CASE("segment vs plane: crossing, parallel, same-side") {
  auto const pl{geo::plane3_d{vec3{0, 0, 1}, 0.0}};
  auto const hit{geo::intersects(geo::segment3_d{vec3{0, 0, -1}, vec3{0, 0, 1}}, pl)};
  REQUIRE(hit.has_value());
  CHECK(*hit == vec3{0, 0, 0});
  CHECK_FALSE(geo::intersects(geo::segment3_d{vec3{0, 0, 1}, vec3{0, 0, 2}}, pl).has_value());
}

TEST_CASE("sphere/circle vs box and plane") {
  geo::aabb3_d const box{vec3{0, 0, 0}, vec3{2, 2, 2}};
  CHECK(geo::intersects(geo::sphere3_d{vec3{3, 1, 1}, 1.5}, box));
  CHECK_FALSE(geo::intersects(geo::sphere3_d{vec3{5, 1, 1}, 1.0}, box));
  CHECK(geo::intersects(geo::sphere3_d{vec3{1, 1, 5}, 1.0}, geo::plane3_d{vec3{0, 0, 1}, -4.5}));
}

TEST_CASE("OBB SAT: 2D and 3D overlap and separation") {
  geo::obb2_d const a{vec2{0, 0}, vec2{1, 1}, nm::radians<double>{0.0}};
  geo::obb2_d const b{vec2{1.5, 0}, vec2{1, 1}, nm::radians<double>{nm::quarter_pi}};
  CHECK(geo::intersects(a, b));
  geo::obb2_d const far{vec2{5, 0}, vec2{1, 1}, nm::radians<double>{nm::quarter_pi}};
  CHECK_FALSE(geo::intersects(a, far));

  geo::obb3_d const c{vec3{0, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}};
  CHECK(geo::intersects(c, geo::obb3_d{vec3{1.5, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}}));
  CHECK_FALSE(
    geo::intersects(c, geo::obb3_d{vec3{3, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}})
  );

  CHECK(geo::intersects(geo::aabb3_d{vec3{-1, -1, -1}, vec3{1, 1, 1}}, c));
}

TEST_CASE("ray vs OBB reduces to the local-frame box") {
  geo::obb3_d const box{vec3{0, 0, -5}, vec3{1, 1, 1}, nm::quaternion<double>{}};
  auto const hit{geo::intersects(geo::ray3_d{vec3{0, 0, 0}, unit(vec3{0, 0, -1})}, box)};
  REQUIRE(hit.has_value());
  CHECK(*hit == doctest::Approx(4.0));
}

TEST_CASE("capsule overlaps: vs sphere and vs capsule") {
  geo::capsule3_d const cap{vec3{0, 0, 0}, vec3{0, 0, 4}, 1.0};
  CHECK(geo::intersects(cap, geo::sphere3_d{vec3{1.5, 0, 2}, 1.0}));
  CHECK_FALSE(geo::intersects(cap, geo::sphere3_d{vec3{3, 0, 2}, 0.5}));

  geo::capsule3_d const cap2{vec3{1.5, 0, 0}, vec3{1.5, 0, 4}, 1.0};
  CHECK(geo::intersects(cap, cap2));
  geo::capsule3_d const cap3{vec3{3, 0, 0}, vec3{3, 0, 4}, 0.5};
  CHECK_FALSE(geo::intersects(cap, cap3));
}

TEST_CASE("OBB3 SAT accounts for rotation, not just identity axes") {
  geo::obb3_d const a{vec3{0, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}};
  auto const rot{*nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<double>{nm::quarter_pi})};
  CHECK_FALSE(
    geo::intersects(a, geo::obb3_d{vec3{2.2, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}})
  );
  CHECK(geo::intersects(a, geo::obb3_d{vec3{2.2, 0, 0}, vec3{1, 1, 1}, rot}));
}

TEST_CASE("closest_point: triangle projects interior, edge, and vertex regions") {
  geo::triangle3_d const t{vec3{0, 0, 0}, vec3{2, 0, 0}, vec3{0, 2, 0}};
  CHECK(geo::closest_point(t, vec3{0.5, 0.5, 3.0}) == vec3{0.5, 0.5, 0.0});
  CHECK(geo::closest_point(t, vec3{-1, -1, 0}) == vec3{0, 0, 0});
  auto const on_ab{geo::closest_point(t, vec3{1, -1, 0})};
  CHECK(on_ab.x() == doctest::Approx(1.0));
  CHECK(on_ab.y() == doctest::Approx(0.0));
}

template <typename Vec>
auto nearest_on_edges(Vec const& a, Vec const& b, Vec const& c, Vec const& p) -> double {
  auto best{std::numeric_limits<double>::max()};
  for (auto const& [from, to] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}}) {
    auto const q{geo::closest_point(geo::segment<double, Vec::size()>{from, to}, p)};
    best = std::min(best, nm::length(q - p));
  }
  return best;
}

TEST_CASE("closest_point: a collinear triangle returns the nearest point of its edges") {
  vec3 const o{4.704322481253493, -5.818567643344991, 0.8289594898218926};
  vec3 const d{3.915687994460292, -5.4289996332584805, -6.500901506934389};
  geo::triangle3_d const line{
    o + d * 9.643366857512024, o + d * 0.3327178375571993, o + d * -4.783416462458434
  };
  vec3 const p{9.925073982411309, 9.308387027555625, 1.165868960773647};
  CHECK(
    nm::length(geo::closest_point(line, p) - p)
    == doctest::Approx(nearest_on_edges(line.a(), line.b(), line.c(), p))
  );

  // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
  auto rng{std::mt19937{7}};
  auto u{std::uniform_real_distribution<double>{-10.0, 10.0}};
  auto misses{0};
  for (auto i{0}; i < 2000; ++i) {
    vec3 const o3{u(rng), u(rng), u(rng)};
    vec3 const d3{u(rng), u(rng), u(rng)};
    geo::triangle3_d const t3{o3 + d3 * u(rng), o3 + d3 * u(rng), o3 + d3 * u(rng)};
    vec3 const p3{u(rng), u(rng), u(rng)};
    auto const want3{nearest_on_edges(t3.a(), t3.b(), t3.c(), p3)};
    misses += nm::length(geo::closest_point(t3, p3) - p3) > want3 + 1e-9 * (1.0 + want3) ? 1 : 0;

    vec2 const o2{u(rng), u(rng)};
    vec2 const d2{u(rng), u(rng)};
    geo::triangle2_d const t2{o2 + d2 * u(rng), o2 + d2 * u(rng), o2 + d2 * u(rng)};
    vec2 const p2{u(rng), u(rng)};
    auto const want2{nearest_on_edges(t2.a(), t2.b(), t2.c(), p2)};
    misses += nm::length(geo::closest_point(t2, p2) - p2) > want2 + 1e-9 * (1.0 + want2) ? 1 : 0;
  }
  CHECK(misses == 0);
}

TEST_CASE("closest_point: an obb clamps an outside point onto its surface") {
  geo::obb3_d const box{vec3{0, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}};
  CHECK(geo::closest_point(box, vec3{5, 0, 0}) == vec3{1, 0, 0});
  CHECK(geo::closest_point(box, vec3{0.5, 0.5, 0.5}) == vec3{0.5, 0.5, 0.5});

  auto const rot{*nm::from_axis_angle(vec3{0, 0, 1}, nm::radians<double>{nm::half_pi_v<double>})};
  geo::obb3_d const turned{vec3{0, 0, 0}, vec3{2, 1, 1}, rot};
  auto const q{geo::closest_point(turned, vec3{0, 5, 0})};
  CHECK(q.y() == doctest::Approx(2.0));
}

TEST_CASE("closest_point: a 2D obb clamps an outside point") {
  geo::obb2_d const box{vec2{0, 0}, vec2{1, 1}, nm::radians<double>{0.0}};
  CHECK(geo::closest_point(box, vec2{5, 0}).x() == doctest::Approx(1.0));
}

TEST_CASE("closest_points: a segment piercing a triangle has zero distance") {
  geo::triangle3_d const t{vec3{-1, -1, 0}, vec3{1, -1, 0}, vec3{0, 1, 0}};
  geo::segment3_d const through{vec3{0, 0, -1}, vec3{0, 0, 1}};
  auto const r{geo::closest_points(through, t)};
  CHECK(nm::length(r.second - r.first) == doctest::Approx(0.0).epsilon(1e-6));
  CHECK(r.first.z() == doctest::Approx(0.0).epsilon(1e-6));
}

TEST_CASE("closest_points: a segment above a triangle projects onto the face") {
  geo::triangle3_d const t{vec3{-2, -2, 0}, vec3{2, -2, 0}, vec3{0, 2, 0}};
  geo::segment3_d const above{vec3{0, 0, 2}, vec3{0, 0, 5}};
  auto const r{geo::closest_points(above, t)};
  CHECK(r.first.z() == doctest::Approx(2.0));
  CHECK(r.second.z() == doctest::Approx(0.0));
  CHECK(nm::length(r.second - r.first) == doctest::Approx(2.0));
}

TEST_CASE("intersects: the pairwise matrix is symmetric in the argument order") {
  geo::aabb3_d const box{vec3{0, 0, 0}, vec3{2, 2, 2}};
  geo::sphere3_d const s{vec3{3, 1, 1}, 1.5};
  CHECK(geo::intersects(box, s) == geo::intersects(s, box));

  geo::obb3_d const o{vec3{0, 0, 0}, vec3{1, 1, 1}, nm::quaternion<double>{}};
  CHECK(geo::intersects(o, box) == geo::intersects(box, o));

  geo::plane3_d const pl{vec3{0, 0, 1}, -4.5};
  geo::sphere3_d const s2{vec3{1, 1, 5}, 1.0};
  CHECK(geo::intersects(pl, s2) == geo::intersects(s2, pl));

  geo::capsule3_d const cap{vec3{0, 0, 0}, vec3{0, 0, 4}, 1.0};
  geo::sphere3_d const s3{vec3{1.5, 0, 2}, 1.0};
  CHECK(geo::intersects(s3, cap) == geo::intersects(cap, s3));

  auto const r{geo::ray3_d{vec3{0, 0, 0}, unit(vec3{0, 0, -1})}};
  geo::sphere3_d const target{vec3{0, 0, -10}, 1.0};
  CHECK(geo::intersects(target, r) == geo::intersects(r, target));
  auto const hb1{geo::intersects(box, r)};
  auto const hb2{geo::intersects(r, box)};
  CHECK(hb1.has_value() == hb2.has_value());
}

TEST_CASE("intersects: a near-parallel ray does not fabricate a far triangle hit") {
  geo::triangle3_d const t{vec3{-1, 0, -1}, vec3{1, 0, -1}, vec3{0, 0, 1}};
  auto const grazing{geo::ray3_d{vec3{0, 1e-9, 0}, unit(vec3{1, 1e-12, 0})}};
  CHECK_FALSE(geo::intersects(grazing, t).has_value());
}

}  // namespace
