/**
 * @file
 * @brief Tests for nexenne::container::flat_hash_set.
 */

#include <doctest/doctest.h>

#include <string>

#include <nexenne/container/flat_hash_set.hpp>

namespace {

namespace cn = nexenne::container;
using set_t = cn::flat_hash_set<int>;

TEST_CASE("nexenne::container::flat_hash_set insert deduplicates") {
  set_t s;
  CHECK(s.insert(1));        // fresh
  CHECK_FALSE(s.insert(1));  // already present
  CHECK(s.insert(2));
  CHECK(s.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_set contains and count") {
  set_t s;
  s.insert(5);
  CHECK(s.contains(5));
  CHECK_FALSE(s.contains(99));
  CHECK(s.count(5) == 1);
  CHECK(s.count(99) == 0);
}

TEST_CASE("nexenne::container::flat_hash_set erase leaves a reusable tombstone") {
  set_t s;
  s.insert(1);
  s.insert(2);
  s.insert(3);
  CHECK(s.erase(2));
  CHECK(s.size() == 2);
  CHECK_FALSE(s.contains(2));
  CHECK_FALSE(s.erase(99));  // absent

  CHECK(s.insert(4));  // reuses the tombstone
  CHECK(s.contains(4));
  CHECK(s.contains(1));  // a probe still walks past the tombstone
  CHECK(s.contains(3));
}

TEST_CASE("nexenne::container::flat_hash_set grows and rehashes, keeping every element") {
  set_t s;
  for (int i{0}; i < 100; ++i) {
    CHECK(s.insert(i));
  }
  CHECK(s.size() == 100);
  CHECK(s.capacity() >= 100);
  for (int i{0}; i < 100; ++i) {
    CHECK(s.contains(i));
  }
}

TEST_CASE("nexenne::container::flat_hash_set iterates every element once") {
  set_t s;
  s.insert(1);
  s.insert(2);
  s.insert(3);
  int count{0};
  int sum{0};
  for (auto const& v : s) {
    ++count;
    sum += v;
  }
  CHECK(count == 3);
  CHECK(sum == 6);
}

TEST_CASE("nexenne::container::flat_hash_set reserve avoids a rehash") {
  set_t s;
  s.reserve(100);
  auto const reserved{s.capacity()};
  CHECK(reserved >= 100);
  for (int i{0}; i < 50; ++i) {
    s.insert(i);
  }
  CHECK(s.capacity() == reserved);
}

TEST_CASE("nexenne::container::flat_hash_set clear and shrink_to_fit") {
  set_t s;
  for (int i{0}; i < 10; ++i) {
    s.insert(i);
  }
  s.clear();
  CHECK(s.empty());
  CHECK_FALSE(s.contains(5));
  s.insert(1);
  s.shrink_to_fit();
  CHECK(s.contains(1));
}

TEST_CASE("nexenne::container::flat_hash_set swap") {
  set_t a;
  a.insert(1);
  a.insert(2);
  set_t b;
  b.insert(9);
  swap(a, b);
  CHECK(a.size() == 1);
  CHECK(a.contains(9));
  CHECK(b.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_set equality is order-independent") {
  set_t a;
  a.insert(1);
  a.insert(2);
  set_t b;
  b.insert(2);  // inserted in a different order
  b.insert(1);
  set_t c;
  c.insert(1);
  c.insert(3);
  CHECK(a == b);
  CHECK_FALSE(a == c);
}

TEST_CASE("nexenne::container::flat_hash_set works with string elements") {
  cn::flat_hash_set<std::string> s;
  s.insert("alpha");
  s.insert("beta");
  CHECK(s.insert("alpha") == false);  // deduplicated
  CHECK(s.contains("alpha"));
  CHECK_FALSE(s.contains("gamma"));
  CHECK(s.size() == 2);
}

}  // namespace
