/**
 * @file
 * @brief Tests for nexenne::container::flat_set.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/container/flat_set.hpp>

namespace {

namespace cn = nexenne::container;
using set_t = cn::flat_set<int>;

static_assert(std::random_access_iterator<set_t::iterator>);

// flat_set is usable in a constant expression.
static_assert([] {
  set_t s;
  s.insert(3);
  s.insert(1);
  s.insert(2);
  s.insert(1);  // duplicate, ignored
  bool ok{s.size() == 3 && s.contains(2) && !s.contains(5) && *s.begin() == 1};
  ok = ok && s.erase(2) == 1 && s.size() == 2;
  return ok;
}());

TEST_CASE("nexenne::container::flat_set insert keeps sorted order and dedups") {
  set_t s;
  auto const [it1, ok1]{s.insert(5)};
  CHECK(ok1);
  CHECK(*it1 == 5);
  s.insert(1);
  s.insert(3);
  auto const [it2, ok2]{s.insert(3)};  // duplicate
  CHECK_FALSE(ok2);
  CHECK(*it2 == 3);
  CHECK(s.size() == 3);

  std::vector<int> const v(s.begin(), s.end());
  CHECK(v == std::vector{1, 3, 5});
}

TEST_CASE("nexenne::container::flat_set initializer list sorts and dedups") {
  set_t s{4, 2, 4, 1, 2};
  CHECK(s.size() == 3);
  std::vector<int> const v(s.begin(), s.end());
  CHECK(v == std::vector{1, 2, 4});
}

TEST_CASE("nexenne::container::flat_set find, contains, count") {
  set_t s{1, 3, 5};
  REQUIRE(s.find(3) != s.end());
  CHECK(*s.find(3) == 3);
  CHECK(s.find(4) == s.end());
  CHECK(s.contains(5));
  CHECK_FALSE(s.contains(2));
  CHECK(s.count(1) == 1);
  CHECK(s.count(2) == 0);
}

TEST_CASE("nexenne::container::flat_set lower_bound and upper_bound") {
  set_t s{10, 20, 30};
  CHECK(*s.lower_bound(20) == 20);  // first >= 20
  CHECK(*s.lower_bound(15) == 20);  // first >= 15
  CHECK(*s.upper_bound(20) == 30);  // first > 20
  CHECK(s.upper_bound(30) == s.end());
}

TEST_CASE("nexenne::container::flat_set erase by key and by iterator") {
  set_t s{1, 2, 3, 4};
  CHECK(s.erase(2) == 1);
  CHECK(s.size() == 3);
  CHECK_FALSE(s.contains(2));
  CHECK(s.erase(99) == 0);  // absent

  auto const next{s.erase(s.find(1))};  // remove 1, return the next
  CHECK(*next == 3);                    // 2 was already gone
  CHECK(s.size() == 2);
}

TEST_CASE("nexenne::container::flat_set emplace") {
  cn::flat_set<std::string> s;
  auto const [it, ok]{s.emplace("hello")};
  CHECK(ok);
  CHECK(*it == "hello");
  auto const [it2, ok2]{s.emplace("hello")};  // duplicate
  CHECK_FALSE(ok2);
  CHECK(s.size() == 1);
}

TEST_CASE("nexenne::container::flat_set swap") {
  set_t a{1, 2};
  set_t b{7, 8, 9};
  swap(a, b);
  CHECK(a.size() == 3);
  CHECK(b.size() == 2);
  CHECK(a.contains(7));
}

TEST_CASE("nexenne::container::flat_set equality and ordering") {
  set_t a{1, 2, 3};
  set_t b{3, 2, 1};  // same elements
  set_t c{1, 2, 4};
  CHECK(a == b);
  CHECK(a != c);
  CHECK(a < c);  // lexicographic: [1,2,3] < [1,2,4]
}

TEST_CASE("nexenne::container::flat_set honours a custom comparator") {
  cn::flat_set<int, std::greater<int>> s{1, 2, 3};
  std::vector<int> const v(s.begin(), s.end());
  CHECK(v == std::vector{3, 2, 1});  // descending order
  CHECK(*s.begin() == 3);
  CHECK(*s.lower_bound(2) == 2);  // first not greater-ordered before 2
}

TEST_CASE("nexenne::container::flat_set move insert leaves the source moved-from") {
  cn::flat_set<std::string> s;
  std::string value{"a string long enough to dodge the small-string buffer"};
  auto const [it, ok]{s.insert(std::move(value))};
  CHECK(ok);
  CHECK(*it == "a string long enough to dodge the small-string buffer");
  // A duplicate move insert reports false and does not consume the argument's
  // visible value (it is only moved on a real insertion).
  std::string again{"a string long enough to dodge the small-string buffer"};
  auto const [it2, ok2]{s.insert(std::move(again))};
  CHECK_FALSE(ok2);
  CHECK(again == "a string long enough to dodge the small-string buffer");
  CHECK(s.size() == 1);
}

TEST_CASE("nexenne::container::flat_set equal_range mimics lower/upper bound") {
  set_t s{10, 20, 30};
  // flat_set has no equal_range; lower_bound/upper_bound bracket a present and
  // an absent key the std way.
  CHECK(s.lower_bound(20) != s.end());
  CHECK(*s.lower_bound(20) == 20);
  CHECK(std::distance(s.lower_bound(20), s.upper_bound(20)) == 1);  // present: width 1
  CHECK(std::distance(s.lower_bound(25), s.upper_bound(25)) == 0);  // absent: empty range
  CHECK(s.lower_bound(5) == s.begin());
  CHECK(s.upper_bound(30) == s.end());
}

TEST_CASE("nexenne::container::flat_set empty set queries return misses, never UB") {
  set_t s;
  CHECK(s.empty());
  CHECK(s.size() == 0);
  CHECK(s.find(1) == s.end());
  CHECK_FALSE(s.contains(1));
  CHECK(s.count(1) == 0);
  CHECK(s.erase(1) == 0);  // erase of an absent key on an empty set
  CHECK(s.lower_bound(1) == s.end());
  CHECK(s.upper_bound(1) == s.end());
  CHECK(s.begin() == s.end());
}

TEST_CASE("nexenne::container::flat_set stays sorted across many inserts and erases") {
  set_t s;
  for (int const k : {50, 10, 90, 30, 70, 20, 80, 40, 60, 0}) {
    s.insert(k);
  }
  CHECK(std::is_sorted(s.begin(), s.end()));
  CHECK(s.size() == 10);
  // Erase a scattered subset, then add more, and confirm the sort holds.
  for (int const k : {30, 0, 90, 50}) {
    CHECK(s.erase(k) == 1);
  }
  for (int const k : {35, 5, 95, 55}) {
    s.insert(k);
  }
  CHECK(std::is_sorted(s.begin(), s.end()));
  std::vector<int> const v(s.begin(), s.end());
  CHECK(v == std::vector{5, 10, 20, 35, 40, 55, 60, 70, 80, 95});
}

TEST_CASE("nexenne::container::flat_set clear and reserve keep the container usable") {
  set_t s{1, 2, 3};
  s.reserve(64);
  CHECK(s.size() == 3);
  s.clear();
  CHECK(s.empty());
  CHECK(s.find(2) == s.end());
  s.insert(7);
  CHECK(s.contains(7));
  s.shrink_to_fit();
  CHECK(s.contains(7));
}

TEST_CASE("nexenne::container::flat_set with std::string keys survives churn under sanitizers") {
  cn::flat_set<std::string> s;
  for (auto const& word : {"delta", "alpha", "charlie", "bravo", "echo", "alpha"}) {
    s.insert(std::string{word});
  }
  CHECK(s.size() == 5);
  CHECK(std::is_sorted(s.begin(), s.end()));
  CHECK(s.contains("alpha"));
  CHECK(s.erase("charlie") == 1);
  CHECK_FALSE(s.contains("charlie"));
  std::vector<std::string> const v(s.begin(), s.end());
  CHECK(v == std::vector<std::string>{"alpha", "bravo", "delta", "echo"});
}

TEST_CASE("nexenne::container::flat_set differential against std::set with string keys") {
  cn::flat_set<std::string> flat;
  std::set<std::string> ref;
  std::mt19937 rng{20260619};
  std::uniform_int_distribution<int> key_dist{0, 60};
  std::uniform_int_distribution<int> op_dist{0, 2};
  for (int step{0}; step < 4000; ++step) {
    auto const key{"key-" + std::to_string(key_dist(rng))};
    switch (op_dist(rng)) {
      case 0: {
        auto const [it, ok]{flat.insert(key)};
        auto const ref_ok{ref.insert(key).second};
        CHECK(ok == ref_ok);
        break;
      }
      case 1: {
        CHECK(flat.erase(key) == ref.erase(key));
        break;
      }
      default: {
        CHECK(flat.contains(key) == (ref.count(key) != 0));
        break;
      }
    }
    CHECK(flat.size() == ref.size());
  }
  CHECK(std::equal(flat.begin(), flat.end(), ref.begin(), ref.end()));
}

}  // namespace
