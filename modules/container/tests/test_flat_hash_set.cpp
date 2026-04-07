/**
 * @file
 * @brief Tests for nexenne::container::flat_hash_set.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <random>
#include <string>
#include <unordered_set>

#include <nexenne/container/flat_hash_set.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;
using set_t = cn::flat_hash_set<int>;

// A hash that funnels every element into one bucket, forcing the probe sequence
// and tombstone handling to do real work.
struct colliding_hash {
  [[nodiscard]] auto operator()(int) const noexcept -> std::size_t {
    return 0;
  }
};

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

TEST_CASE("nexenne::container::flat_hash_set resolves heavy collisions correctly") {
  cn::flat_hash_set<int, colliding_hash> s;
  for (int i{0}; i < 50; ++i) {
    CHECK(s.insert(i));
  }
  CHECK(s.size() == 50);
  for (int i{0}; i < 50; ++i) {
    CHECK(s.contains(i));
  }
  for (int i{0}; i < 50; i += 2) {
    CHECK(s.erase(i));
  }
  CHECK(s.size() == 25);
  for (int i{0}; i < 50; ++i) {
    CHECK(s.contains(i) == (i % 2 != 0));
  }
  for (int i{0}; i < 50; i += 2) {
    CHECK(s.insert(i));  // reuses tombstones along the single probe chain
  }
  CHECK(s.size() == 50);
  for (int i{0}; i < 50; ++i) {
    CHECK(s.contains(i));
  }
}

TEST_CASE("nexenne::container::flat_hash_set erase-then-reinsert churn keeps membership exact") {
  set_t s;
  for (int i{0}; i < 30; ++i) {
    s.insert(i);
  }
  for (int round{0}; round < 40; ++round) {
    for (int i{0}; i < 30; ++i) {
      CHECK(s.erase(i));
    }
    CHECK(s.empty());
    for (int i{0}; i < 30; ++i) {
      CHECK(s.insert(i));
    }
    CHECK(s.size() == 30);
    for (int i{0}; i < 30; ++i) {
      CHECK(s.contains(i));
    }
  }
}

TEST_CASE("nexenne::container::flat_hash_set load_factor and the expected-entries constructor") {
  set_t empty;
  CHECK(empty.capacity() == 0);
  CHECK(empty.load_factor() == doctest::Approx(0.0));
  CHECK(empty.max_size() > 0);

  set_t sized{100};  // reserve up front
  CHECK(sized.capacity() >= 100);
  auto const reserved{sized.capacity()};
  for (int i{0}; i < 50; ++i) {
    sized.insert(i);
  }
  CHECK(sized.capacity() == reserved);  // no rehash within the reservation
  CHECK(sized.load_factor() > 0.0);
  CHECK(sized.load_factor() < 1.0);
}

TEST_CASE("nexenne::container::flat_hash_set empty set queries return misses, never UB") {
  set_t s;
  CHECK(s.empty());
  CHECK(s.size() == 0);
  CHECK_FALSE(s.contains(1));
  CHECK(s.count(1) == 0);
  CHECK_FALSE(s.erase(1));
  CHECK(s.begin() == s.end());
}

TEST_CASE("nexenne::container::flat_hash_set const iteration walks every element") {
  set_t s;
  s.insert(10);
  s.insert(20);
  s.insert(30);
  auto const& cs{s};
  int count{0};
  int sum{0};
  for (auto const& v : cs) {
    ++count;
    sum += v;
  }
  CHECK(count == 3);
  CHECK(sum == 60);
  CHECK(cs.cbegin() != cs.cend());
}

TEST_CASE("nexenne::container::flat_hash_set with string elements survives churn") {
  cn::flat_hash_set<std::string> s;
  for (auto const& word : {"delta", "alpha", "charlie", "bravo", "echo", "alpha"}) {
    s.insert(std::string{word});
  }
  CHECK(s.size() == 5);
  CHECK(s.contains("alpha"));
  CHECK(s.erase("charlie"));
  CHECK_FALSE(s.contains("charlie"));
  CHECK(s.insert(std::string{"foxtrot"}));
  CHECK(s.size() == 5);
}

TEST_CASE("nexenne::container::flat_hash_set differential against std::unordered_set") {
  cn::flat_hash_set<std::string> flat;
  std::unordered_set<std::string> ref;
  std::mt19937 rng{20260623};
  std::uniform_int_distribution<int> key_dist{0, 80};
  std::uniform_int_distribution<int> op_dist{0, 2};
  for (int step{0}; step < 6000; ++step) {
    auto const key{"k" + std::to_string(key_dist(rng))};
    switch (op_dist(rng)) {
      case 0: {
        auto const flat_new{flat.insert(key)};
        auto const ref_new{ref.insert(key).second};
        CHECK(flat_new == ref_new);
        break;
      }
      case 1: {
        CHECK(flat.erase(key) == (ref.erase(key) != 0));
        break;
      }
      default: {
        CHECK(flat.contains(key) == (ref.count(key) != 0));
        break;
      }
    }
    CHECK(flat.size() == ref.size());
  }
  for (auto const& key : ref) {
    CHECK(flat.contains(key));
  }
  std::size_t flat_count{0};
  for (auto const& v : flat) {
    nexenne::utility::discard(v);
    ++flat_count;
  }
  CHECK(flat_count == ref.size());
}

}  // namespace
