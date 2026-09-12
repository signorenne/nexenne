/**
 * @file
 * @brief Tests for nexenne::container::static_flat_map.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/static_flat_map.hpp>

namespace {

namespace cn = nexenne::container;
using map_t = cn::static_flat_map<int, int, 4>;

static_assert(map_t::capacity() == 4);

static_assert([] {
  map_t m;
  bool ok{m.insert({3, 30}).has_value() && m.insert({1, 10}).has_value()};
  ok = ok && m.size() == 2 && m.contains(1) && m.begin()->first == 1;
  ok = ok && m.erase(3) == 1 && m.size() == 1;
  return ok;
}());

constexpr cn::static_flat_map<int, int, 3> const_table{{3, 30}, {1, 10}, {2, 20}};
static_assert(const_table.size() == 3);
static_assert(const_table.begin()->first == 1);
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
  auto const r2{m.insert({3, 99})};
  REQUIRE(r2.has_value());
  auto const [it2, ins2]{*r2};
  CHECK_FALSE(ins2);
  CHECK(it2->second == 30);

  CHECK(m.insert({7, 70}).has_value());
  CHECK(m.full());
  CHECK(m.insert({9, 90}).error() == cn::container_error::full);
  CHECK(m.insert({5, 55}).has_value());
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
  CHECK((*a).second);
  auto const b{m.insert_or_assign(5, 55)};
  REQUIRE(b.has_value());
  CHECK_FALSE((*b).second);
  CHECK(*m.at(5) == 55);

  m.insert_or_assign(1, 1);
  m.insert_or_assign(2, 2);
  m.insert_or_assign(3, 3);
  CHECK(m.full());
  CHECK(m.insert_or_assign(9, 9).error() == cn::container_error::full);
  CHECK(m.insert_or_assign(5, 99).has_value());
}

TEST_CASE("nexenne::container::static_flat_map try_emplace constructs only on insert") {
  cn::static_flat_map<int, std::string, 2> m;
  auto const a{m.try_emplace(1, "hello")};
  REQUIRE(a.has_value());
  CHECK((*a).second);
  CHECK((*a).first->second == "hello");
  auto const b{m.try_emplace(1, "world")};
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
  map_t m{{3, 30}, {1, 10}, {2, 20}, {1, 99}};
  CHECK(m.size() == 3);
  CHECK(*m.at(1) == 10);
  std::vector<int> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
  }
  CHECK(keys == std::vector{1, 2, 3});
}

TEST_CASE("nexenne::container::static_flat_map initializer list drops entries past capacity") {
  cn::static_flat_map<int, int, 4> m{{5, 50}, {1, 10}, {4, 40}, {2, 20}, {3, 30}};
  CHECK(m.size() == 4);
  CHECK(m.full());
  std::vector<int> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
  }
  CHECK(keys == std::vector{1, 2, 4, 5});
  CHECK(m.at(3) == nullptr);
}

TEST_CASE("nexenne::container::static_flat_map emplace reports full on a new key") {
  cn::static_flat_map<int, int, 2> m;
  CHECK(m.emplace(std::pair<int, int>{1, 10}).has_value());
  CHECK(m.emplace(std::pair<int, int>{2, 20}).has_value());
  CHECK(m.full());
  auto const dup{m.emplace(std::pair<int, int>{1, 99})};
  REQUIRE(dup.has_value());
  CHECK_FALSE((*dup).second);
  CHECK(*m.at(1) == 10);
  CHECK(m.emplace(std::pair<int, int>{3, 30}).error() == cn::container_error::full);
}

TEST_CASE("nexenne::container::static_flat_map try_emplace reports full on a new key") {
  cn::static_flat_map<int, std::string, 2> m;
  CHECK(m.try_emplace(1, "one").has_value());
  CHECK(m.try_emplace(2, "two").has_value());
  CHECK(m.full());
  CHECK(m.try_emplace(3, "three").error() == cn::container_error::full);
  CHECK(m.try_emplace(1, "ignored").has_value());
  CHECK(*m.at(1) == "one");
}

TEST_CASE("nexenne::container::static_flat_map move insert consumes only on a real insertion") {
  cn::static_flat_map<int, std::string, 2> m;
  auto entry{std::pair<int, std::string>{1, "a value comfortably past the SSO buffer length"}};
  auto const r1{m.insert(std::move(entry))};
  REQUIRE(r1.has_value());
  CHECK((*r1).second);
  CHECK((*r1).first->second == "a value comfortably past the SSO buffer length");
  auto again{std::pair<int, std::string>{1, "this duplicate value should not be consumed"}};
  auto const r2{m.insert(std::move(again))};
  REQUIRE(r2.has_value());
  CHECK_FALSE((*r2).second);
  CHECK(again.second == "this duplicate value should not be consumed");
}

TEST_CASE("nexenne::container::static_flat_map insert_or_assign self-aliasing is safe") {
  cn::static_flat_map<int, std::string, 2> m;
  m.insert_or_assign(1, "a stored value that is definitely longer than the SSO buffer");
  auto const r{m.insert_or_assign(1, *m.at(1))};
  REQUIRE(r.has_value());
  CHECK_FALSE((*r).second);
  CHECK(*m.at(1) == "a stored value that is definitely longer than the SSO buffer");
}

TEST_CASE("nexenne::container::static_flat_map holds a move-only mapped type") {
  cn::static_flat_map<int, std::unique_ptr<int>, 3> m;
  CHECK(m.try_emplace(2, std::make_unique<int>(20)).has_value());
  CHECK(m.try_emplace(1, std::make_unique<int>(10)).has_value());
  CHECK(m.insert({3, std::make_unique<int>(30)}).has_value());
  REQUIRE(m.at(1) != nullptr);
  CHECK(**m.at(1) == 10);
  CHECK(**m.at(3) == 30);
  CHECK(m.erase(2) == 1);
  CHECK(m.at(2) == nullptr);
  CHECK(**m.at(3) == 30);
  CHECK(m.size() == 2);
}

TEST_CASE("nexenne::container::static_flat_map empty map queries return misses, never UB") {
  map_t m;
  CHECK(m.empty());
  CHECK_FALSE(m.full());
  CHECK(m.find(1) == m.end());
  CHECK_FALSE(m.contains(1));
  CHECK(m.count(1) == 0);
  CHECK(m.at(1) == nullptr);
  CHECK(m.erase(1) == 0);
  CHECK(m.lower_bound(1) == m.end());
  CHECK(m.upper_bound(1) == m.end());
  CHECK(m.begin() == m.end());
}

TEST_CASE("nexenne::container::static_flat_map stays key-sorted across fill, erase, refill") {
  cn::static_flat_map<int, int, 8> m;
  for (int const k : {50, 10, 80, 30, 70, 20, 60, 40}) {
    CHECK(m.insert({k, k * 2}).has_value());
  }
  CHECK(m.full());
  CHECK(std::is_sorted(m.begin(), m.end(), [](auto const& a, auto const& b) {
    return a.first < b.first;
  }));
  for (int const k : {30, 80, 10}) {
    CHECK(m.erase(k) == 1);
  }
  for (int const k : {35, 5, 95}) {
    CHECK(m.insert({k, k * 2}).has_value());
  }
  CHECK(m.full());
  CHECK(std::is_sorted(m.begin(), m.end(), [](auto const& a, auto const& b) {
    return a.first < b.first;
  }));
  std::vector<int> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
    CHECK(v == k * 2);
  }
  CHECK(keys == std::vector{5, 20, 35, 40, 50, 60, 70, 95});
}

TEST_CASE("nexenne::container::static_flat_map with string keys and values survives churn") {
  cn::static_flat_map<std::string, std::string, 4> m;
  CHECK(m.insert_or_assign("delta", "value-delta-beyond-the-small-string-buffer").has_value());
  CHECK(m.insert_or_assign("alpha", "value-alpha-beyond-the-small-string-buffer").has_value());
  CHECK(m.insert_or_assign("charlie", "value-charlie-beyond-the-small-string-buffer").has_value());
  CHECK(m.full() == false);
  CHECK(m.insert_or_assign("bravo", "value-bravo-beyond-the-small-string-buffer").has_value());
  CHECK(m.full());
  CHECK(m.insert_or_assign("echo", "x").error() == cn::container_error::full);
  CHECK(m.insert_or_assign("alpha", "alpha-overwritten-and-still-quite-long").has_value());
  CHECK(*m.at("alpha") == "alpha-overwritten-and-still-quite-long");
  CHECK(m.erase("charlie") == 1);
  std::vector<std::string> keys;
  for (auto const& [k, v] : m) {
    keys.push_back(k);
  }
  CHECK(keys == std::vector<std::string>{"alpha", "bravo", "delta"});
}

TEST_CASE("nexenne::container::static_flat_map differential against std::map within capacity") {
  constexpr std::size_t cap{16};
  cn::static_flat_map<int, int, cap> flat;
  std::map<int, int> ref;
  std::mt19937 rng{20260621};
  std::uniform_int_distribution<int> key_dist{0, 40};
  std::uniform_int_distribution<int> val_dist{0, 1000};
  std::uniform_int_distribution<int> op_dist{0, 3};
  for (int step{0}; step < 5000; ++step) {
    auto const key{key_dist(rng)};
    auto const val{val_dist(rng)};
    switch (op_dist(rng)) {
      case 0: {
        auto const present{ref.find(key) != ref.end()};
        if (present || ref.size() < cap) {
          CHECK(flat.insert_or_assign(key, val).has_value());
          ref[key] = val;
        } else {
          CHECK(flat.insert_or_assign(key, val).error() == cn::container_error::full);
        }
        break;
      }
      case 1: {
        auto const present{ref.find(key) != ref.end()};
        if (present || ref.size() < cap) {
          auto const r{flat.insert({key, val})};
          REQUIRE(r.has_value());
          auto const ref_ok{ref.insert({key, val}).second};
          CHECK((*r).second == ref_ok);
        } else {
          CHECK(flat.insert({key, val}).error() == cn::container_error::full);
        }
        break;
      }
      case 2: {
        CHECK(flat.erase(key) == ref.erase(key));
        break;
      }
      default: {
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
    CHECK(flat.size() <= cap);
  }
  std::vector<std::pair<int, int>> flat_entries{flat.begin(), flat.end()};
  std::vector<std::pair<int, int>> ref_entries{ref.begin(), ref.end()};
  CHECK(flat_entries == ref_entries);
}

TEST_CASE(
  "nexenne::container::static_flat_map try_emplace does not read a moved-from aliased arg"
) {
  cn::static_flat_map<int, std::string, 8> m;
  std::string const payload{"a long string that will not fit in any SSO buffer"};
  REQUIRE(m.try_emplace(5, payload).has_value());
  REQUIRE(m.try_emplace(1, *m.at(5)).has_value());
  CHECK(*m.at(1) == payload);
  CHECK(*m.at(5) == payload);

  cn::static_flat_map<int, std::string, 8> n;
  REQUIRE(n.insert_or_assign(5, payload).has_value());
  REQUIRE(n.insert_or_assign(1, *n.at(5)).has_value());
  CHECK(*n.at(1) == payload);
  CHECK(*n.at(5) == payload);
}

TEST_CASE("nexenne::container::static_flat_map heterogeneous lookup avoids constructing a key") {
  cn::static_flat_map<std::string, int, 8, std::less<>> m;
  REQUIRE(m.insert({"alpha", 1}).has_value());
  REQUIRE(m.insert({"gamma", 3}).has_value());
  CHECK(m.contains(std::string_view{"alpha"}));
  CHECK(m.count(std::string_view{"gamma"}) == 1);
  CHECK_FALSE(m.contains(std::string_view{"beta"}));
  REQUIRE(m.find(std::string_view{"gamma"}) != m.end());
  CHECK(m.find(std::string_view{"gamma"})->second == 3);
  REQUIRE(m.at(std::string_view{"alpha"}) != nullptr);
  CHECK(*m.at(std::string_view{"alpha"}) == 1);
  CHECK(m.lower_bound(std::string_view{"alpha"})->first == "alpha");
  CHECK(m.upper_bound(std::string_view{"alpha"})->first == "gamma");
}

}  // namespace

TEST_CASE("nexenne::container::static_flat_map key_comp exposes the stored comparator") {
  static_assert(std::is_same_v<decltype(const_table.key_comp()), std::less<int> const&>);
  CHECK(const_table.key_comp()(1, 2));
}
