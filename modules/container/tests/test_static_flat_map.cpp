/**
 * @file
 * @brief Tests for nexenne::container::static_flat_map.
 */

#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <nexenne/container/static_flat_map.hpp>

namespace {

namespace cn = nexenne::container;
using map_t = cn::static_flat_map<int, int, 4>;

static_assert(map_t::capacity() == 4);

// static_flat_map is usable in a constant expression.
static_assert([] {
  map_t m;
  bool ok{m.insert({3, 30}).has_value() && m.insert({1, 10}).has_value()};
  ok = ok && m.size() == 2 && m.contains(1) && m.begin()->first == 1;
  ok = ok && m.erase(3) == 1 && m.size() == 1;
  return ok;
}());

// Built from an initializer list, a constexpr instance is a compile-time
// constant lookup table.
constexpr cn::static_flat_map<int, int, 3> const_table{{3, 30}, {1, 10}, {2, 20}};
static_assert(const_table.size() == 3);
static_assert(const_table.begin()->first == 1);  // stored sorted by key
static_assert(*const_table.at(2) == 20);
static_assert(const_table.at(99) == nullptr);

TEST_CASE("nexenne::container::static_flat_map insert orders, dedups, reports full") {
  map_t m;
  auto const r1{m.insert({5, 50})};
  REQUIRE(r1.has_value());
  auto const [it1, ins1]{*r1};
  CHECK(ins1);
  CHECK(it1->first == 5);

  CHECK(m.insert({1, 10}).has_value());
  CHECK(m.insert({3, 30}).has_value());
  auto const r2{m.insert({3, 99})};  // duplicate key
  REQUIRE(r2.has_value());
  auto const [it2, ins2]{*r2};
  CHECK_FALSE(ins2);
  CHECK(it2->second == 30);  // original kept

  CHECK(m.insert({7, 70}).has_value());  // size is now 4
  CHECK(m.full());
  CHECK(m.insert({9, 90}).error() == cn::container_error::full);  // full and new
  CHECK(m.insert({5, 55}).has_value());  // a duplicate into a full map is fine
  CHECK(m.size() == 4);

  std::vector<int> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
  }
  CHECK(keys == std::vector{1, 3, 5, 7});
}

TEST_CASE("nexenne::container::static_flat_map at returns a pointer or nullptr") {
  map_t m;
  m.insert({1, 10});
  REQUIRE(m.at(1) != nullptr);
  CHECK(*m.at(1) == 10);
  CHECK(m.at(99) == nullptr);
  *m.at(1) = 11;
  CHECK(*m.at(1) == 11);
}

TEST_CASE("nexenne::container::static_flat_map insert_or_assign overwrites or inserts") {
  map_t m;
  auto const a{m.insert_or_assign(5, 50)};
  REQUIRE(a.has_value());
  CHECK((*a).second);  // inserted
  auto const b{m.insert_or_assign(5, 55)};
  REQUIRE(b.has_value());
  CHECK_FALSE((*b).second);  // overwritten
  CHECK(*m.at(5) == 55);

  m.insert_or_assign(1, 1);
  m.insert_or_assign(2, 2);
  m.insert_or_assign(3, 3);
  CHECK(m.full());
  CHECK(m.insert_or_assign(9, 9).error() == cn::container_error::full);  // full and new
  CHECK(m.insert_or_assign(5, 99).has_value());                          // overwrite is fine
}

TEST_CASE("nexenne::container::static_flat_map try_emplace constructs only on insert") {
  cn::static_flat_map<int, std::string, 2> m;
  auto const a{m.try_emplace(1, "hello")};
  REQUIRE(a.has_value());
  CHECK((*a).second);
  CHECK((*a).first->second == "hello");
  auto const b{m.try_emplace(1, "world")};  // key present, value untouched
  REQUIRE(b.has_value());
  CHECK_FALSE((*b).second);
  CHECK((*b).first->second == "hello");
}

TEST_CASE("nexenne::container::static_flat_map find, contains, count, bounds") {
  map_t m;
  m.insert({10, 1});
  m.insert({20, 2});
  m.insert({30, 3});
  REQUIRE(m.find(20) != m.end());
  CHECK(m.find(20)->second == 2);
  CHECK(m.find(15) == m.end());
  CHECK(m.contains(30));
  CHECK(m.count(99) == 0);
  CHECK(m.lower_bound(15)->first == 20);
  CHECK(m.upper_bound(20)->first == 30);
}

TEST_CASE("nexenne::container::static_flat_map erase by key and iterator, clear") {
  map_t m;
  m.insert({1, 1});
  m.insert({2, 2});
  m.insert({3, 3});
  CHECK(m.erase(2) == 1);
  CHECK(m.size() == 2);
  CHECK(m.erase(99) == 0);
  auto const next{m.erase(m.find(1))};
  CHECK(next->first == 3);
  m.clear();
  CHECK(m.empty());
}

TEST_CASE("nexenne::container::static_flat_map mutates a value in place") {
  map_t m;
  m.insert({1, 10});
  m.find(1)->second = 999;
  CHECK(*m.at(1) == 999);
}

TEST_CASE("nexenne::container::static_flat_map swap and comparison") {
  map_t a;
  a.insert({1, 1});
  a.insert({2, 2});
  map_t b;
  b.insert({2, 2});
  b.insert({1, 1});
  map_t c;
  c.insert({1, 1});
  c.insert({2, 9});
  CHECK(a == b);
  CHECK(a != c);

  map_t d;
  d.insert({7, 7});
  swap(a, d);
  CHECK(a.size() == 1);
  CHECK(a.contains(7));
}

TEST_CASE("nexenne::container::static_flat_map builds from an initializer list") {
  map_t m{{3, 30}, {1, 10}, {2, 20}, {1, 99}};  // sorted, the later 1 dropped
  CHECK(m.size() == 3);
  CHECK(*m.at(1) == 10);  // the first 1 is kept
  std::vector<int> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
  }
  CHECK(keys == std::vector{1, 2, 3});
}

}  // namespace
