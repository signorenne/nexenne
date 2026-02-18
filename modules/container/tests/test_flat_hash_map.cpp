/**
 * @file
 * @brief Tests for nexenne::container::flat_hash_map.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nexenne/container/flat_hash_map.hpp>

namespace {

namespace cn = nexenne::container;
using map_t = cn::flat_hash_map<int, int>;

// A pathological hash that funnels every key into the same bucket, forcing the
// probe sequence, tombstone handling, and rehash logic to do real work.
struct colliding_hash {
  [[nodiscard]] auto operator()(int) const noexcept -> std::size_t {
    return 0;
  }
};

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

TEST_CASE("nexenne::container::flat_hash_map operator[] with a moved-from key does not SEGV") {
  // Regression guard: operator[] copies the key before inserting, so moving the
  // caller's key in must still find the freshly inserted slot.
  cn::flat_hash_map<std::string, int> m;
  std::string key{"a key well past the small-string optimisation buffer length"};
  m[std::move(key)] = 42;
  CHECK(m.size() == 1);
  REQUIRE(m.find("a key well past the small-string optimisation buffer length") != nullptr);
  CHECK(*m.find("a key well past the small-string optimisation buffer length") == 42);
  // Indexing again with an existing entry's own stored key (self-aliasing read).
  auto const stored{m.begin()->first};
  CHECK(m[stored] == 42);
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::flat_hash_map insert_or_assign self-aliasing is safe") {
  cn::flat_hash_map<int, std::string> m;
  m.insert_or_assign(1, "a stored value comfortably longer than the SSO buffer");
  // Feed the slot's own value straight back: insert_or_assign takes Value by
  // value, so the copy is made before the slot is overwritten.
  m.insert_or_assign(1, *m.at(1));
  REQUIRE(m.find(1) != nullptr);
  CHECK(*m.find(1) == "a stored value comfortably longer than the SSO buffer");
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::flat_hash_map resolves heavy collisions correctly") {
  cn::flat_hash_map<int, int, colliding_hash> m;
  for (int i{0}; i < 50; ++i) {
    CHECK(m.insert(i, i * 100));
  }
  CHECK(m.size() == 50);
  for (int i{0}; i < 50; ++i) {
    REQUIRE(m.find(i) != nullptr);
    CHECK(*m.find(i) == i * 100);
  }
  // Erase every even key, leaving a long tombstone run all in one bucket chain.
  for (int i{0}; i < 50; i += 2) {
    CHECK(m.erase(i));
  }
  CHECK(m.size() == 25);
  for (int i{0}; i < 50; ++i) {
    CHECK(m.contains(i) == (i % 2 != 0));
  }
  // Reinsert the erased keys; tombstones must be reused, not leak the probe chain.
  for (int i{0}; i < 50; i += 2) {
    CHECK(m.insert(i, i * 100));
  }
  CHECK(m.size() == 50);
  for (int i{0}; i < 50; ++i) {
    REQUIRE(m.find(i) != nullptr);
    CHECK(*m.find(i) == i * 100);
  }
}

TEST_CASE("nexenne::container::flat_hash_map erase-then-reinsert churn keeps lookups exact") {
  map_t m;
  for (int i{0}; i < 30; ++i) {
    m.insert(i, i);
  }
  // Repeatedly erase and reinsert, accumulating tombstones until a rehash clears
  // them; the table must keep returning the right answers throughout.
  for (int round{0}; round < 40; ++round) {
    for (int i{0}; i < 30; ++i) {
      CHECK(m.erase(i));
    }
    CHECK(m.empty());
    for (int i{0}; i < 30; ++i) {
      CHECK(m.insert(i, i + round));
    }
    CHECK(m.size() == 30);
    for (int i{0}; i < 30; ++i) {
      REQUIRE(m.find(i) != nullptr);
      CHECK(*m.find(i) == i + round);
    }
  }
}

TEST_CASE("nexenne::container::flat_hash_map load_factor and capacity edges") {
  map_t empty;
  CHECK(empty.capacity() == 0);
  CHECK(empty.load_factor() == doctest::Approx(0.0));

  map_t m;
  m.insert(1, 1);
  CHECK(m.capacity() == map_t::initial_capacity);  // first allocation is the floor
  CHECK(m.load_factor() > 0.0);
  CHECK(m.load_factor() < 1.0);
  // Drive past the 7/8 threshold of the initial 16 slots to force exactly one grow.
  for (int i{0}; i < 14; ++i) {
    m.insert(100 + i, i);
  }
  CHECK(m.capacity() > map_t::initial_capacity);
  CHECK(m.load_factor() <= 0.875);
}

TEST_CASE("nexenne::container::flat_hash_map shrink_to_fit clears tombstones") {
  map_t m;
  for (int i{0}; i < 200; ++i) {
    m.insert(i, i);
  }
  auto const grown{m.capacity()};
  for (int i{0}; i < 190; ++i) {
    CHECK(m.erase(i));
  }
  CHECK(m.size() == 10);
  m.shrink_to_fit();
  CHECK(m.capacity() < grown);  // released the now-oversized table
  for (int i{190}; i < 200; ++i) {
    REQUIRE(m.find(i) != nullptr);
    CHECK(*m.find(i) == i);
  }
  // Reinsert below the freed capacity to confirm the shrunk table still works.
  m.insert(0, 0);
  CHECK(m.contains(0));
}

TEST_CASE("nexenne::container::flat_hash_map empty map queries return misses, never UB") {
  map_t m;
  CHECK(m.empty());
  CHECK(m.size() == 0);
  CHECK(m.find(1) == nullptr);
  CHECK_FALSE(m.contains(1));
  CHECK(m.count(1) == 0);
  CHECK(m.at(1) == nullptr);
  CHECK_FALSE(m.erase(1));
  CHECK(m.begin() == m.end());
  auto const& cm{m};
  CHECK(cm.find(1) == nullptr);
  CHECK(cm.begin() == cm.end());
}

TEST_CASE("nexenne::container::flat_hash_map const iteration and accessors") {
  map_t m;
  m.insert(1, 10);
  m.insert(2, 20);
  auto const& cm{m};
  int count{0};
  int key_sum{0};
  for (auto const& [k, v] : cm) {
    ++count;
    key_sum += k;
    CHECK(v == k * 10);
  }
  CHECK(count == 2);
  CHECK(key_sum == 3);
  CHECK(cm.cbegin() != cm.cend());
  // The hasher and predicate accessors are reachable and usable.
  CHECK(cm.hash_function()(7) == std::hash<int>{}(7));
  CHECK(cm.key_eq()(3, 3));
  CHECK_FALSE(cm.key_eq()(3, 4));
}

TEST_CASE("nexenne::container::flat_hash_map with string keys and values survives churn") {
  cn::flat_hash_map<std::string, std::string> m;
  m.insert("alpha", "value-alpha-past-the-small-string-buffer");
  m.insert_or_assign("beta", "value-beta-past-the-small-string-buffer");
  m["gamma"] = "value-gamma-past-the-small-string-buffer";
  CHECK(m.size() == 3);
  m.insert_or_assign("alpha", "alpha-overwritten-and-also-fairly-long-here");
  REQUIRE(m.find("alpha") != nullptr);
  CHECK(*m.find("alpha") == "alpha-overwritten-and-also-fairly-long-here");
  CHECK(m.erase("beta"));
  CHECK(m.find("beta") == nullptr);
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::flat_hash_map differential against std::unordered_map") {
  cn::flat_hash_map<std::string, int> flat;
  std::unordered_map<std::string, int> ref;
  std::mt19937 rng{20260622};
  std::uniform_int_distribution<int> key_dist{0, 80};
  std::uniform_int_distribution<int> val_dist{0, 1000};
  std::uniform_int_distribution<int> op_dist{0, 4};
  for (int step{0}; step < 6000; ++step) {
    auto const key{"k" + std::to_string(key_dist(rng))};
    auto const val{val_dist(rng)};
    switch (op_dist(rng)) {
      case 0: {  // insert (no overwrite)
        auto const flat_new{flat.insert(key, val)};
        auto const ref_new{ref.insert({key, val}).second};
        CHECK(flat_new == ref_new);
        break;
      }
      case 1: {  // insert_or_assign (overwrite)
        flat.insert_or_assign(key, val);
        ref[key] = val;
        break;
      }
      case 2: {  // operator[]
        flat[key] = val;
        ref[key] = val;
        break;
      }
      case 3: {  // erase
        CHECK(flat.erase(key) == (ref.erase(key) != 0));
        break;
      }
      default: {  // lookup
        auto const* const p{flat.find(key)};
        auto const it{ref.find(key)};
        if (it == ref.end()) {
          CHECK(p == nullptr);
        } else {
          REQUIRE(p != nullptr);
          CHECK(*p == it->second);
        }
        break;
      }
    }
    CHECK(flat.size() == ref.size());
  }
  // Same multiset of entries (iteration order is unspecified, so compare contents).
  for (auto const& [k, v] : ref) {
    auto const* const p{flat.find(k)};
    REQUIRE(p != nullptr);
    CHECK(*p == v);
  }
  std::size_t flat_count{0};
  for (auto const& entry : flat) {
    static_cast<void>(entry);
    ++flat_count;
  }
  CHECK(flat_count == ref.size());
}

}  // namespace
