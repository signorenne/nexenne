/**
 * @file
 * @brief Tests for nexenne::container::flat_map.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
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

TEST_CASE("nexenne::container::flat_map heterogeneous lower_bound and upper_bound") {
  cn::flat_map<std::string, int, std::less<>> m{{"alpha", 1}, {"charlie", 3}, {"echo", 5}};
  CHECK(m.lower_bound(std::string_view{"bravo"})->first == "charlie");
  CHECK(m.lower_bound(std::string_view{"charlie"})->first == "charlie");
  // upper_bound has no heterogeneous overload (only lower_bound does); use a key.
  CHECK(m.upper_bound(std::string{"charlie"})->first == "echo");
  CHECK(m.lower_bound(std::string_view{"zulu"}) == m.end());
  auto const& cm{m};
  CHECK(cm.lower_bound(std::string_view{"bravo"})->first == "charlie");
}

TEST_CASE("nexenne::container::flat_map emplace splits like insert on a duplicate key") {
  cn::flat_map<int, std::string> m;
  auto const [it1, ok1]{m.emplace(1, "hello")};
  CHECK(ok1);
  CHECK(it1->second == "hello");
  auto const [it2, ok2]{m.emplace(1, "world")};  // duplicate key, no overwrite
  CHECK_FALSE(ok2);
  CHECK(it2->second == "hello");
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::flat_map move insert and insert vs insert_or_assign split") {
  cn::flat_map<int, std::string> m;
  // Moving an entry in: on a real insertion the source is consumed.
  auto entry{std::pair<int, std::string>{1, "this value is intentionally long enough"}};
  auto const [it1, ok1]{m.insert(std::move(entry))};
  CHECK(ok1);
  CHECK(it1->second == "this value is intentionally long enough");
  // insert of a duplicate key keeps the existing value (no overwrite).
  auto const [it2, ok2]{m.insert({1, "ignored"})};
  CHECK_FALSE(ok2);
  CHECK(it2->second == "this value is intentionally long enough");
  // insert_or_assign of the same key DOES overwrite.
  auto const [it3, ok3]{m.insert_or_assign(1, "fresh")};
  CHECK_FALSE(ok3);  // existing key, so not newly inserted
  CHECK(it3->second == "fresh");
}

TEST_CASE("nexenne::container::flat_map insert_or_assign self-aliasing is safe") {
  cn::flat_map<int, std::string> m;
  m.insert_or_assign(1, "a string deliberately past the small-string optimisation buffer");
  // Pass the map's own stored value back through insert_or_assign. The header
  // takes Value by value, so the argument is copied before the slot is touched;
  // this must not corrupt or self-destruct.
  m.insert_or_assign(1, *m.at(1));
  CHECK(*m.at(1) == "a string deliberately past the small-string optimisation buffer");
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::flat_map operator[] with a moved-from key argument") {
  cn::flat_map<std::string, int> m;
  std::string key{"a key past the small-string optimisation threshold for sure"};
  m[key] = 7;                     // copy-inserts, key untouched
  CHECK(m[std::move(key)] == 7);  // operator[] takes the key by const ref, no move hazard
  CHECK(m.size() == 1);
  // Self-aliasing operator[]: index with an existing entry's own key.
  auto const stored{m.begin()->first};
  CHECK(m[stored] == 7);
  CHECK(m.size() == 1);
}

TEST_CASE("nexenne::container::flat_map holds a move-only mapped type via try_emplace") {
  cn::flat_map<int, std::unique_ptr<std::string>> m;
  auto const [it1, ok1]{m.try_emplace(2, std::make_unique<std::string>("two"))};
  CHECK(ok1);
  CHECK(*it1->second == "two");
  // A second try_emplace on the present key does not construct or consume.
  auto const [it2, ok2]{m.try_emplace(2, std::make_unique<std::string>("ignored"))};
  CHECK_FALSE(ok2);
  CHECK(*it2->second == "two");
  // emplace with a moved unique_ptr pair also works.
  m.emplace(std::pair<int, std::unique_ptr<std::string>>{1, std::make_unique<std::string>("one")});
  REQUIRE(m.at(1) != nullptr);
  CHECK(**m.at(1) == "one");
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::flat_map empty map queries return misses, never UB") {
  cn::flat_map<int, int> m;
  CHECK(m.empty());
  CHECK(m.find(1) == m.end());
  CHECK_FALSE(m.contains(1));
  CHECK(m.count(1) == 0);
  CHECK(m.at(1) == nullptr);
  CHECK(m.erase(1) == 0);
  CHECK(m.lower_bound(1) == m.end());
  CHECK(m.upper_bound(1) == m.end());
  CHECK(m.begin() == m.end());
  auto const& cm{m};
  CHECK(cm.at(1) == nullptr);
  CHECK(cm.find(1) == cm.end());
}

TEST_CASE("nexenne::container::flat_map stays key-sorted across churn and operator[] inserts") {
  cn::flat_map<int, int> m;
  for (int const k : {50, 10, 90, 30, 70, 20, 80, 40, 60, 0}) {
    m[k] = k * 2;
  }
  CHECK(std::is_sorted(m.begin(), m.end(), [](auto const& a, auto const& b) {
    return a.first < b.first;
  }));
  for (int const k : {30, 0, 90}) {
    CHECK(m.erase(k) == 1);
  }
  for (int const k : {35, 5, 95}) {
    m.insert_or_assign(k, k * 2);
  }
  CHECK(std::is_sorted(m.begin(), m.end(), [](auto const& a, auto const& b) {
    return a.first < b.first;
  }));
  std::vector<int> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
    CHECK(v == k * 2);
  }
  CHECK(keys == std::vector{5, 10, 20, 35, 40, 50, 60, 70, 80, 95});
}

TEST_CASE("nexenne::container::flat_map with string keys and values survives churn") {
  cn::flat_map<std::string, std::string> m;
  m.insert_or_assign("alpha", "first value beyond the small-string buffer");
  m.insert_or_assign("beta", "second value beyond the small-string buffer");
  m["gamma"] = "third value beyond the small-string buffer";
  CHECK(m.size() == 3);
  m.insert_or_assign("alpha", "alpha overwritten value, also reasonably long");
  CHECK(*m.at("alpha") == "alpha overwritten value, also reasonably long");
  CHECK(m.erase("beta") == 1);
  CHECK(m.at("beta") == nullptr);
  CHECK(std::is_sorted(m.begin(), m.end(), [](auto const& a, auto const& b) {
    return a.first < b.first;
  }));
}

TEST_CASE("nexenne::container::flat_map differential against std::map with string keys") {
  cn::flat_map<std::string, int> flat;
  std::map<std::string, int> ref;
  std::mt19937 rng{20260620};
  std::uniform_int_distribution<int> key_dist{0, 50};
  std::uniform_int_distribution<int> val_dist{0, 1000};
  std::uniform_int_distribution<int> op_dist{0, 4};
  for (int step{0}; step < 5000; ++step) {
    auto const key{"k" + std::to_string(key_dist(rng))};
    auto const val{val_dist(rng)};
    switch (op_dist(rng)) {
      case 0: {  // insert (no overwrite on a duplicate key)
        auto const flat_ok{flat.insert({key, val}).second};
        auto const ref_ok{ref.insert({key, val}).second};
        CHECK(flat_ok == ref_ok);
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
        CHECK(flat.erase(key) == ref.erase(key));
        break;
      }
      default: {  // lookup
        auto const* const p{flat.at(key)};
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
  // Identical contents, in the same sorted key order.
  std::vector<std::pair<std::string, int>> flat_entries{flat.begin(), flat.end()};
  std::vector<std::pair<std::string, int>> ref_entries{ref.begin(), ref.end()};
  CHECK(flat_entries == ref_entries);
}

}  // namespace
