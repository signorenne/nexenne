/**
 * @file
 * @brief Tests for nexenne::container::flat_hash_map.
 */

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <utility>

#include <nexenne/container/flat_hash_map.hpp>

namespace {

namespace cn = nexenne::container;
using map_t = cn::flat_hash_map<int, int>;

TEST_CASE("nexenne::container::flat_hash_map insert keeps, insert_or_assign overwrites") {
  map_t m;
  CHECK(m.insert(1, 10));                  // new
  CHECK_FALSE(m.insert(1, 99));            // present: not overwritten
  CHECK(*m.find(1) == 10);                 // original kept
  CHECK_FALSE(m.insert_or_assign(1, 99));  // overwritten (false = not new)
  CHECK(*m.find(1) == 99);
  CHECK(m.insert_or_assign(2, 20));  // new (true)
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_map find, contains, count, at") {
  map_t m;
  m.insert(5, 50);
  REQUIRE(m.find(5) != nullptr);
  CHECK(*m.find(5) == 50);
  CHECK(m.find(99) == nullptr);
  CHECK(m.contains(5));
  CHECK_FALSE(m.contains(99));
  CHECK(m.count(5) == 1);
  CHECK(m.count(99) == 0);
  REQUIRE(m.at(5) != nullptr);
  CHECK(*m.at(5) == 50);
  CHECK(m.at(99) == nullptr);
}

TEST_CASE("nexenne::container::flat_hash_map operator[] accesses and default-inserts") {
  map_t m;
  m[1] = 10;
  CHECK(m[1] == 10);
  m[1] = 11;
  CHECK(m[1] == 11);
  CHECK(m[99] == 0);  // default-inserts a zero
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_map emplace constructs but does not overwrite") {
  cn::flat_hash_map<int, std::string> m;
  CHECK(m.emplace(1, "hello"));
  CHECK(*m.find(1) == "hello");
  CHECK_FALSE(m.emplace(1, "world"));  // present: not overwritten
  CHECK(*m.find(1) == "hello");
}

TEST_CASE("nexenne::container::flat_hash_map erase leaves a reusable tombstone") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  m.insert(3, 30);
  CHECK(m.erase(2));
  CHECK(m.size() == 2);
  CHECK_FALSE(m.contains(2));
  CHECK_FALSE(m.erase(99));  // absent

  CHECK(m.insert(4, 40));  // reuses the tombstone
  CHECK(m.contains(4));
  CHECK(m.contains(1));  // a probe still walks past the tombstone
  CHECK(m.contains(3));
}

TEST_CASE("nexenne::container::flat_hash_map grows and rehashes, keeping every entry") {
  map_t m;
  for (int i{0}; i < 100; ++i) {
    CHECK(m.insert(i, i * 10));
  }
  CHECK(m.size() == 100);
  CHECK(m.capacity() >= 100);
  for (int i{0}; i < 100; ++i) {
    REQUIRE(m.find(i) != nullptr);
    CHECK(*m.find(i) == i * 10);
  }
}

TEST_CASE("nexenne::container::flat_hash_map iterates every entry once") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  m.insert(3, 30);
  int count{0};
  int key_sum{0};
  int value_sum{0};
  for (auto const& [k, v] : m) {
    ++count;
    key_sum += k;
    value_sum += v;
  }
  CHECK(count == 3);
  CHECK(key_sum == 6);
  CHECK(value_sum == 60);
}

TEST_CASE("nexenne::container::flat_hash_map reserve avoids a rehash") {
  map_t m;
  m.reserve(100);
  auto const reserved{m.capacity()};
  CHECK(reserved >= 100);
  for (int i{0}; i < 50; ++i) {
    m.insert(i, i);
  }
  CHECK(m.capacity() == reserved);  // stayed within the reserved capacity
}

TEST_CASE("nexenne::container::flat_hash_map clear and shrink_to_fit") {
  map_t m;
  for (int i{0}; i < 10; ++i) {
    m.insert(i, i);
  }
  m.clear();
  CHECK(m.empty());
  CHECK_FALSE(m.contains(5));
  m.insert(1, 1);
  m.shrink_to_fit();
  CHECK(m.contains(1));
}

TEST_CASE("nexenne::container::flat_hash_map swap") {
  map_t a;
  a.insert(1, 1);
  a.insert(2, 2);
  map_t b;
  b.insert(9, 9);
  swap(a, b);
  CHECK(a.size() == 1);
  CHECK(a.contains(9));
  CHECK(b.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_map equality is order-independent") {
  map_t a;
  a.insert(1, 10);
  a.insert(2, 20);
  map_t b;
  b.insert(2, 20);  // inserted in a different order
  b.insert(1, 10);
  map_t c;
  c.insert(1, 10);
  c.insert(2, 99);
  CHECK(a == b);
  CHECK_FALSE(a == c);
}

TEST_CASE("nexenne::container::flat_hash_map works with string keys") {
  cn::flat_hash_map<std::string, int> m;
  m.insert("alpha", 1);
  m.insert("beta", 2);
  CHECK(m.contains("alpha"));
  REQUIRE(m.find("beta") != nullptr);
  CHECK(*m.find("beta") == 2);
  CHECK(m["gamma"] == 0);
}

TEST_CASE("nexenne::container::flat_hash_map holds a move-only value") {
  cn::flat_hash_map<int, std::unique_ptr<int>> m;
  m.insert(1, std::make_unique<int>(10));
  m.emplace(2, std::make_unique<int>(20));
  REQUIRE(m.find(1) != nullptr);
  CHECK(**m.find(1) == 10);
  CHECK(m.size() == 2);
}

}  // namespace
