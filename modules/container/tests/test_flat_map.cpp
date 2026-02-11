/**
 * @file
 * @brief Tests for nexenne::container::flat_map.
 */

#include <doctest/doctest.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/container/flat_map.hpp>

namespace {

namespace cn = nexenne::container;

// flat_map is usable in a constant expression.
static_assert([] {
  cn::flat_map<int, int> m;
  m.insert({3, 30});
  m.insert({1, 10});
  m[2] = 20;
  bool ok{m.size() == 3 && m.contains(2) && m[1] == 10 && m.begin()->first == 1};
  ok = ok && m.erase(2) == 1 && m.size() == 2;
  return ok;
}());

TEST_CASE("nexenne::container::flat_map insert keeps key order and dedups") {
  cn::flat_map<int, int> m;
  auto const [it1, ok1]{m.insert({5, 50})};
  CHECK(ok1);
  CHECK(it1->first == 5);
  m.insert({1, 10});
  m.insert({3, 30});
  auto const [it2, ok2]{m.insert({3, 99})};  // duplicate key
  CHECK_FALSE(ok2);
  CHECK(it2->second == 30);  // the original value is kept
  CHECK(m.size() == 3);

  std::vector<int> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
  }
  CHECK(keys == std::vector{1, 3, 5});
}

TEST_CASE("nexenne::container::flat_map operator[] inserts and accesses") {
  cn::flat_map<int, int> m;
  m[10] = 100;  // inserts
  m[20] = 200;
  CHECK(m.size() == 2);
  CHECK(m[10] == 100);
  m[10] = 111;  // overwrites
  CHECK(m[10] == 111);
  CHECK(m[99] == 0);  // default-inserts a zero
  CHECK(m.size() == 3);
}

TEST_CASE("nexenne::container::flat_map at returns a pointer or nullptr") {
  cn::flat_map<int, int> m;
  m[1] = 10;
  REQUIRE(m.at(1) != nullptr);
  CHECK(*m.at(1) == 10);
  CHECK(m.at(99) == nullptr);  // no throw, just nullptr
  *m.at(1) = 11;               // mutate through the pointer
  CHECK(m[1] == 11);
}

TEST_CASE("nexenne::container::flat_map insert_or_assign") {
  cn::flat_map<int, int> m;
  auto const [it1, inserted1]{m.insert_or_assign(5, 50)};
  CHECK(inserted1);
  CHECK(it1->second == 50);
  auto const [it2, inserted2]{m.insert_or_assign(5, 55)};
  CHECK_FALSE(inserted2);  // overwritten
  CHECK(it2->second == 55);
}

TEST_CASE("nexenne::container::flat_map try_emplace constructs only on insert") {
  cn::flat_map<int, std::string> m;
  auto const [it1, ok1]{m.try_emplace(1, "hello")};
  CHECK(ok1);
  CHECK(it1->second == "hello");
  auto const [it2, ok2]{m.try_emplace(1, "world")};  // key present, value untouched
  CHECK_FALSE(ok2);
  CHECK(it2->second == "hello");
}

TEST_CASE("nexenne::container::flat_map find, contains, count, bounds") {
  cn::flat_map<int, int> m{{10, 1}, {20, 2}, {30, 3}};
  REQUIRE(m.find(20) != m.end());
  CHECK(m.find(20)->second == 2);
  CHECK(m.find(15) == m.end());
  CHECK(m.contains(30));
  CHECK(m.count(99) == 0);
  CHECK(m.lower_bound(15)->first == 20);
  CHECK(m.upper_bound(20)->first == 30);
}

TEST_CASE("nexenne::container::flat_map erase by key and by iterator") {
  cn::flat_map<int, int> m{{1, 1}, {2, 2}, {3, 3}};
  CHECK(m.erase(2) == 1);
  CHECK(m.size() == 2);
  CHECK(m.erase(99) == 0);
  auto const next{m.erase(m.find(1))};
  CHECK(next->first == 3);
}

TEST_CASE("nexenne::container::flat_map mutates a value in place") {
  cn::flat_map<int, int> m{{1, 10}};
  m.find(1)->second = 999;  // value changes, key untouched
  CHECK(m[1] == 999);
}

TEST_CASE("nexenne::container::flat_map swap and comparison") {
  cn::flat_map<int, int> a{{1, 1}, {2, 2}};
  cn::flat_map<int, int> b{{2, 2}, {1, 1}};  // same entries
  cn::flat_map<int, int> c{{1, 1}, {2, 9}};
  CHECK(a == b);
  CHECK(a != c);

  cn::flat_map<int, int> d{{7, 7}};
  swap(a, d);
  CHECK(a.size() == 1);
  CHECK(a.contains(7));
  CHECK(d.size() == 2);
}

TEST_CASE("nexenne::container::flat_map honours a custom comparator") {
  cn::flat_map<int, int, std::greater<int>> m{{1, 1}, {3, 3}, {2, 2}};
  std::vector<int> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
  }
  CHECK(keys == std::vector{3, 2, 1});  // descending keys
}

TEST_CASE("nexenne::container::flat_map heterogeneous lookup with a transparent comparator") {
  cn::flat_map<std::string, int, std::less<>> m{{"alpha", 1}, {"beta", 2}, {"gamma", 3}};
  // string_view lookups resolve without constructing a temporary std::string.
  REQUIRE(m.find(std::string_view{"beta"}) != m.end());
  CHECK(m.find(std::string_view{"beta"})->second == 2);
  CHECK(m.find(std::string_view{"missing"}) == m.end());
  CHECK(m.contains(std::string_view{"gamma"}));
  CHECK_FALSE(m.contains(std::string_view{"delta"}));
  CHECK(m.count(std::string_view{"alpha"}) == 1);
  REQUIRE(m.at(std::string_view{"alpha"}) != nullptr);
  CHECK(*m.at(std::string_view{"alpha"}) == 1);
  CHECK(m.at(std::string_view{"nope"}) == nullptr);
  auto const& cm{m};
  REQUIRE(cm.find(std::string_view{"beta"}) != cm.end());
  CHECK(cm.find(std::string_view{"beta"})->second == 2);
}

// A transparent comparator enables the heterogeneous overload; the default
// (non-transparent) std::less<Key> does not. Detect via named concepts so the
// negative case is a clean substitution failure, not a hard error.
template <typename M>
concept sv_findable = requires(M& m) { m.find(std::string_view{"x"}); };
static_assert(sv_findable<cn::flat_map<std::string, int, std::less<>>>);
static_assert(!sv_findable<cn::flat_map<std::string, int>>);

}  // namespace
