/**
 * @file
 * @brief Tests for nexenne::container::slot_map.
 */

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/container/slot_map.hpp>

namespace {

namespace cn = nexenne::container;
using map_t = cn::slot_map<int>;

TEST_CASE("nexenne::container::slot_map insert returns a stable key, find resolves it") {
  map_t m;
  auto const a{m.insert(10)};
  auto const b{m.insert(20)};
  CHECK(m.size() == 2);
  REQUIRE(m.find(a) != nullptr);
  CHECK(*m.find(a) == 10);
  CHECK(*m.find(b) == 20);
  CHECK(m.contains(a));
}

TEST_CASE("nexenne::container::slot_map a default key never matches") {
  map_t m;
  map_t::key const null_key;
  CHECK(m.find(null_key) == nullptr);
  m.insert(1);                         // fills slot 0, generation 1
  CHECK(m.find(null_key) == nullptr);  // null key (generation 0) still misses
}

TEST_CASE("nexenne::container::slot_map erase invalidates only that key") {
  map_t m;
  auto const a{m.insert(10)};
  auto const b{m.insert(20)};
  CHECK(m.erase(a));
  CHECK(m.find(a) == nullptr);  // a is gone
  CHECK_FALSE(m.contains(a));
  REQUIRE(m.find(b) != nullptr);  // b still valid
  CHECK(*m.find(b) == 20);
  CHECK(m.size() == 1);
  CHECK_FALSE(m.erase(a));  // double erase is a no-op
}

TEST_CASE("nexenne::container::slot_map recycles a slot but the old key stays stale (ABA guard)") {
  map_t m;
  auto const a{m.insert(10)};
  CHECK(m.erase(a));
  auto const b{m.insert(99)};     // reuses slot 0 with a bumped generation
  CHECK(b.index() == a.index());  // same slot
  CHECK(b.generation() != a.generation());
  CHECK(m.find(a) == nullptr);  // the stale key does not see the new occupant
  REQUIRE(m.find(b) != nullptr);
  CHECK(*m.find(b) == 99);
}

TEST_CASE("nexenne::container::slot_map emplace constructs in place") {
  cn::slot_map<std::string> m;
  auto const k{m.emplace(3, 'x')};  // std::string(3, 'x') == "xxx"
  REQUIRE(m.find(k) != nullptr);
  CHECK(*m.find(k) == "xxx");
}

TEST_CASE("nexenne::container::slot_map iterates only live elements, skipping vacancies") {
  map_t m;
  auto const a{m.insert(1)};
  m.insert(2);
  auto const c{m.insert(3)};
  m.erase(a);  // slot 0 vacant
  m.erase(c);  // last slot vacant; only 2 remains in the middle
  int count{0};
  int sum{0};
  for (int const v : m) {
    ++count;
    sum += v;
  }
  CHECK(count == 1);
  CHECK(sum == 2);
}

TEST_CASE("nexenne::container::slot_map clear invalidates every key, retains capacity") {
  map_t m;
  m.reserve(8);
  auto const cap{m.capacity()};
  auto const a{m.insert(1)};
  auto const b{m.insert(2)};
  m.clear();
  CHECK(m.empty());
  CHECK(m.find(a) == nullptr);
  CHECK(m.find(b) == nullptr);
  CHECK(m.capacity() == cap);  // storage retained
  // slots are reusable after clear
  auto const c{m.insert(3)};
  CHECK(*m.find(c) == 3);
}

TEST_CASE("nexenne::container::slot_map swap exchanges contents, keys stay valid against their map"
) {
  map_t a;
  auto const ka{a.insert(1)};
  map_t b;
  auto const kb{b.insert(9)};
  b.insert(8);
  swap(a, b);
  CHECK(a.size() == 2);
  CHECK(b.size() == 1);
  REQUIRE(a.find(kb) != nullptr);  // kb now resolves against a
  CHECK(*a.find(kb) == 9);
  CHECK(*b.find(ka) == 1);
}

TEST_CASE("nexenne::container::slot_map holds a move-only type") {
  cn::slot_map<std::unique_ptr<int>> m;
  auto const a{m.insert(std::make_unique<int>(10))};
  auto const b{m.emplace(std::make_unique<int>(20))};
  REQUIRE(m.find(a) != nullptr);
  CHECK(**m.find(a) == 10);
  CHECK(**m.find(b) == 20);
  CHECK(m.erase(a));
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::slot_map const iteration and key comparison") {
  map_t m;
  m.insert(5);
  m.insert(7);
  map_t const& cm{m};
  int total{0};
  for (int const v : cm) {
    total += v;
  }
  CHECK(total == 12);

  map_t::key const k1{1, 2};
  map_t::key const k2{1, 2};
  map_t::key const k3{1, 3};
  CHECK(k1 == k2);
  CHECK(k1 != k3);
}

}  // namespace
