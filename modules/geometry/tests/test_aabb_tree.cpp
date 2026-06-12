/**
 * @file
 * @brief Tests for the dynamic AABB tree (aabb_tree.hpp): insert / remove /
 *        update churn, region queries and raycasts against a brute-force oracle.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <vector>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/aabb_tree.hpp>
#include <nexenne/geometry/ray.hpp>
#include <nexenne/math/vector.hpp>

namespace {

namespace geo = nexenne::geometry;
namespace nm = nexenne::math;

using tree3 = geo::aabb_tree<std::uint32_t, 3, float>;
using box3 = geo::aabb<float, 3>;
using ray3 = geo::ray<float, 3>;
using vec3 = nm::vector3_f;

auto make_box(float cx, float cy, float cz, float half = 0.5f) noexcept -> box3 {
  return box3{vec3{cx - half, cy - half, cz - half}, vec3{cx + half, cy + half, cz + half}};
}

TEST_CASE("aabb_tree: empty by default") {
  auto t{tree3{}};
  CHECK(t.empty());
  CHECK(t.size() == 0);
  CHECK(t.height() == 0);
}

TEST_CASE("aabb_tree: single insert is found by an overlapping query") {
  auto t{tree3{}};
  auto const h{t.insert(make_box(0, 0, 0), 42u)};
  CHECK(t.size() == 1);
  CHECK(h != tree3::null_handle);

  auto hits{std::vector<std::uint32_t>{}};
  t.query(make_box(0, 0, 0), [&](auto, std::uint32_t const& p) noexcept { hits.push_back(p); });
  REQUIRE(hits.size() == 1);
  CHECK(hits[0] == 42u);
}

TEST_CASE("aabb_tree: query skips boxes outside the region") {
  auto t{tree3{}};
  t.insert(make_box(0, 0, 0), 1u);
  t.insert(make_box(10, 10, 10), 2u);

  auto hits{std::vector<std::uint32_t>{}};
  t.query(make_box(0, 0, 0, 0.5f), [&](auto, std::uint32_t const& p) noexcept {
    hits.push_back(p);
  });
  REQUIRE(hits.size() == 1);
  CHECK(hits[0] == 1u);
}

TEST_CASE("aabb_tree: remove drops the leaf and keeps the others") {
  auto t{tree3{}};
  auto const h1{t.insert(make_box(0, 0, 0), 1u)};
  t.insert(make_box(5, 5, 5), 2u);
  CHECK(t.remove(h1));
  CHECK(t.size() == 1);

  auto hits{0};
  t.query(make_box(0, 0, 0), [&](auto, std::uint32_t const&) noexcept { ++hits; });
  CHECK(hits == 0);
  hits = 0;
  t.query(make_box(5, 5, 5), [&](auto, std::uint32_t const&) noexcept { ++hits; });
  CHECK(hits == 1);
}

TEST_CASE("aabb_tree: remove releases the leaf payload") {
  // M4 reproduction. A caller-chosen payload can own a resource, so remove must
  // release it now, not pin it until the slot is reused. Track a shared_ptr.
  using owning_tree = geo::aabb_tree<std::shared_ptr<int>, 3, float>;
  auto t{owning_tree{}};
  auto payload{std::make_shared<int>(7)};
  REQUIRE(payload.use_count() == 1);
  auto const h{t.insert(make_box(0, 0, 0), payload)};
  CHECK(payload.use_count() == 2);  // the tree holds a copy.
  CHECK(t.remove(h));
  CHECK(payload.use_count() == 1);  // removal dropped the tree's copy.
}

TEST_CASE("aabb_tree: remove of an invalid handle returns false") {
  auto t{tree3{}};
  CHECK_FALSE(t.remove(tree3::null_handle));
  CHECK_FALSE(t.remove(999u));
}

TEST_CASE("aabb_tree: update inside the fat box does no work") {
  auto t{tree3{0.5f}};  // generous padding.
  auto const h{t.insert(make_box(0, 0, 0, 0.5f), 1u)};
  CHECK_FALSE(t.update(h, make_box(0.1f, 0, 0, 0.5f)));  // still inside fat box.
}

TEST_CASE("aabb_tree: update outside the fat box restructures and relocates") {
  auto t{tree3{0.1f}};  // small padding.
  auto const h{t.insert(make_box(0, 0, 0, 0.5f), 1u)};
  CHECK(t.update(h, make_box(5, 5, 5, 0.5f)));

  auto hits{std::vector<std::uint32_t>{}};
  t.query(make_box(5, 5, 5), [&](auto, std::uint32_t const& p) noexcept { hits.push_back(p); });
  REQUIRE(hits.size() == 1);
  CHECK(hits[0] == 1u);

  hits.clear();
  t.query(make_box(0, 0, 0), [&](auto, std::uint32_t const& p) noexcept { hits.push_back(p); });
  CHECK(hits.empty());
}

TEST_CASE("aabb_tree: raycast hits boxes on the ray and skips off-axis ones") {
  auto t{tree3{}};
  t.insert(make_box(5, 0, 0), 1u);
  t.insert(make_box(10, 0, 0), 2u);
  t.insert(make_box(0, 5, 0), 3u);  // off-axis.

  auto hits{std::vector<std::uint32_t>{}};
  auto const r{ray3{vec3{}, vec3{1, 0, 0}}};
  t.raycast(r, 100.0f, [&](auto, std::uint32_t const& p, float) noexcept { hits.push_back(p); });
  REQUIRE(hits.size() == 2);
  auto has{[&](std::uint32_t id) { return std::find(hits.begin(), hits.end(), id) != hits.end(); }};
  CHECK(has(1u));
  CHECK(has(2u));
  CHECK_FALSE(has(3u));
}

TEST_CASE("aabb_tree: raycast respects max_t") {
  auto t{tree3{}};
  t.insert(make_box(5, 0, 0), 1u);
  t.insert(make_box(50, 0, 0), 2u);

  auto hits{std::vector<std::uint32_t>{}};
  auto const r{ray3{vec3{}, vec3{1, 0, 0}}};
  t.raycast(r, 20.0f, [&](auto, std::uint32_t const& p, float) noexcept { hits.push_back(p); });
  REQUIRE(hits.size() == 1);
  CHECK(hits[0] == 1u);
}

TEST_CASE("aabb_tree: a closest-hit raycast prunes by returning a smaller max_t") {
  auto t{tree3{}};
  t.insert(make_box(50, 0, 0), 2u);  // inserted first, but farther.
  t.insert(make_box(5, 0, 0), 1u);

  auto best_id{tree3::null_handle};
  auto best_t{std::numeric_limits<float>::max()};
  auto const r{ray3{vec3{}, vec3{1, 0, 0}}};
  // Returning the hit distance as the new max_t keeps the nearest hit and prunes
  // any leaf that begins farther along the ray.
  t.raycast(r, 100.0f, [&](auto, std::uint32_t const& p, float t_hit) noexcept -> float {
    if (t_hit < best_t) {
      best_t = t_hit;
      best_id = p;
    }
    return best_t;
  });
  CHECK(best_id == 1u);
}

TEST_CASE("aabb_tree: a query visitor can stop early by returning false") {
  auto t{tree3{}};
  for (auto i{0}; i < 10; ++i) {
    t.insert(make_box(static_cast<float>(i), 0, 0), static_cast<std::uint32_t>(i));
  }
  auto count{0};
  t.query(make_box(0, 0, 0, 20.0f), [&](auto, std::uint32_t const&) noexcept -> bool {
    ++count;
    return count < 3;  // stop after three.
  });
  CHECK(count == 3);
}

TEST_CASE("aabb_tree: clear empties everything") {
  auto t{tree3{}};
  for (auto i{0}; i < 20; ++i) {
    t.insert(make_box(static_cast<float>(i), 0, 0), static_cast<std::uint32_t>(i));
  }
  t.clear();
  CHECK(t.empty());
  CHECK(t.size() == 0);
  CHECK(t.height() == 0);
}

TEST_CASE("aabb_tree: at(handle) returns the fat box and payload") {
  auto t{tree3{}};
  auto const h{t.insert(make_box(2, 3, 4), 99u)};
  auto const got{t.at(h)};
  REQUIRE(got.has_value());
  CHECK(got->second == 99u);
  CHECK(got->first.min().x() < 2.0f);  // fattened outward.
  CHECK(got->first.max().x() > 2.0f);
}

TEST_CASE("aabb_tree: a 2D tree queries correctly") {
  using tree2 = geo::aabb_tree<std::uint32_t, 2, float>;
  using box2 = geo::aabb<float, 2>;
  using vec2 = nm::vector<float, 2>;

  auto t{tree2{}};
  t.insert(box2{vec2{0, 0}, vec2{1, 1}}, 1u);
  t.insert(box2{vec2{5, 5}, vec2{6, 6}}, 2u);

  auto hits{std::vector<std::uint32_t>{}};
  t.query(box2{vec2{0, 0}, vec2{1, 1}}, [&](auto, std::uint32_t const& p) noexcept {
    hits.push_back(p);
  });
  REQUIRE(hits.size() == 1);
  CHECK(hits[0] == 1u);
}

TEST_CASE("aabb_tree: survives many inserts then removals") {
  auto t{tree3{}};
  auto handles{std::vector<tree3::handle_type>{}};
  for (auto i{0}; i < 100; ++i) {
    handles.push_back(t.insert(
      make_box(static_cast<float>(i % 10), static_cast<float>(i / 10), 0),
      static_cast<std::uint32_t>(i)
    ));
  }
  CHECK(t.size() == 100);
  for (auto const h : handles) {
    CHECK(t.remove(h));
  }
  CHECK(t.empty());
}

TEST_CASE("aabb_tree: region queries match a brute-force oracle over random boxes") {
  // Build a tree and a parallel flat list, then check that a batch of random
  // query boxes returns exactly the same overlap set (modulo the fat-box
  // padding, which can only ADD candidates, so the tree result is a superset).
  auto rng{std::mt19937{12345}};
  auto coord{std::uniform_real_distribution<float>{-20.0f, 20.0f}};
  auto half{std::uniform_real_distribution<float>{0.2f, 1.5f}};

  auto t{tree3{0.0f}};  // no padding: tree boxes equal the stored boxes exactly.
  auto boxes{std::vector<box3>{}};
  for (auto i{0}; i < 200; ++i) {
    auto const b{make_box(coord(rng), coord(rng), coord(rng), half(rng))};
    boxes.push_back(b);
    t.insert(b, static_cast<std::uint32_t>(i));
  }

  for (auto q{0}; q < 100; ++q) {
    auto const region{make_box(coord(rng), coord(rng), coord(rng), half(rng) + 1.0f)};

    auto expected{std::vector<std::uint32_t>{}};
    for (auto i{std::size_t{0}}; i < boxes.size(); ++i) {
      if (intersects(boxes[i], region)) {
        expected.push_back(static_cast<std::uint32_t>(i));
      }
    }

    auto got{std::vector<std::uint32_t>{}};
    t.query(region, [&](auto, std::uint32_t const& p) noexcept { got.push_back(p); });
    std::sort(got.begin(), got.end());

    // Every brute-force overlap must be reported by the tree.
    for (auto const id : expected) {
      CHECK(std::find(got.begin(), got.end(), id) != got.end());
    }
  }
}

}  // namespace
