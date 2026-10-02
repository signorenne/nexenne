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
#include <utility>
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

TEST_CASE("aabb_tree: root_bounds spans every fat leaf box") {
  auto t{tree3{0.5f}};
  CHECK_FALSE(t.root_bounds().has_value());

  t.insert(make_box(0, 0, 0), 1u);
  auto const far{t.insert(make_box(4, 0, 0), 2u)};
  auto const both{t.root_bounds()};
  REQUIRE(both.has_value());
  CHECK(both->min() == vec3{-1, -1, -1});
  CHECK(both->max() == vec3{5, 1, 1});

  CHECK(t.remove(far));
  auto const one{t.root_bounds()};
  REQUIRE(one.has_value());
  CHECK(one->max() == vec3{1, 1, 1});
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
  using owning_tree = geo::aabb_tree<std::shared_ptr<int>, 3, float>;
  auto t{owning_tree{}};
  auto payload{std::make_shared<int>(7)};
  REQUIRE(payload.use_count() == 1);
  auto const h{t.insert(make_box(0, 0, 0), payload)};
  CHECK(payload.use_count() == 2);
  CHECK(t.remove(h));
  CHECK(payload.use_count() == 1);
}

TEST_CASE("aabb_tree: remove of an invalid handle returns false") {
  auto t{tree3{}};
  CHECK_FALSE(t.remove(tree3::null_handle));
  CHECK_FALSE(t.remove(999u));
}

TEST_CASE("aabb_tree: update inside the fat box does no work") {
  auto t{tree3{0.5f}};
  auto const h{t.insert(make_box(0, 0, 0, 0.5f), 1u)};
  CHECK_FALSE(t.update(h, make_box(0.1f, 0, 0, 0.5f)));
}

TEST_CASE("aabb_tree: update outside the fat box restructures and relocates") {
  auto t{tree3{0.1f}};
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
  t.insert(make_box(0, 5, 0), 3u);

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
  t.insert(make_box(50, 0, 0), 2u);
  t.insert(make_box(5, 0, 0), 1u);

  auto best_id{tree3::null_handle};
  auto best_t{std::numeric_limits<float>::max()};
  auto const r{ray3{vec3{}, vec3{1, 0, 0}}};
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
    return count < 3;
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
  CHECK(got->first.min().x() < 2.0f);
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
  auto rng{std::mt19937{12345}};
  auto coord{std::uniform_real_distribution<float>{-20.0f, 20.0f}};
  auto half{std::uniform_real_distribution<float>{0.2f, 1.5f}};

  auto t{tree3{0.0f}};
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

    for (auto const id : expected) {
      CHECK(std::find(got.begin(), got.end(), id) != got.end());
    }
  }
}

// NOLINTBEGIN(performance-noexcept-move-constructor,cert-oop54-cpp): a minimal throwing payload
struct throwing_payload {
  int v{0};

  throwing_payload() = default;

  throwing_payload(throwing_payload const& other) : v{other.v} {}

  throwing_payload(throwing_payload&& other) : v{other.v} {}

  auto operator=(throwing_payload const& other) -> throwing_payload& {
    v = other.v;
    return *this;
  }

  auto operator=(throwing_payload&& other) -> throwing_payload& {
    v = other.v;
    return *this;
  }
};

// NOLINTEND(performance-noexcept-move-constructor,cert-oop54-cpp)

using throwing_tree = geo::aabb_tree<throwing_payload, 3, float>;

inline constexpr auto throwing_visit{[](tree3::handle_type, std::uint32_t const&) {}};
inline constexpr auto nothrow_visit{[](tree3::handle_type, std::uint32_t const&) noexcept {}};
inline constexpr auto throwing_hit{[](tree3::handle_type, std::uint32_t const&, float) {}};
inline constexpr auto nothrow_hit{[](tree3::handle_type, std::uint32_t const&, float) noexcept {}};
static_assert(!noexcept(std::declval<tree3 const&>().query(box3{}, throwing_visit)));
static_assert(noexcept(std::declval<tree3 const&>().query(box3{}, nothrow_visit)));
static_assert(!noexcept(std::declval<tree3 const&>().raycast(ray3{}, 1.0F, throwing_hit)));
static_assert(noexcept(std::declval<tree3 const&>().raycast(ray3{}, 1.0F, nothrow_hit)));
static_assert(!noexcept(std::declval<throwing_tree&>().insert(box3{}, throwing_payload{})));
static_assert(noexcept(std::declval<tree3&>().insert(box3{}, 1U)));
static_assert(!noexcept(std::declval<throwing_tree&>().remove(0)));
static_assert(!noexcept(std::declval<throwing_tree const&>().at(0)));
static_assert(noexcept(std::declval<tree3 const&>().at(0)));

}  // namespace
