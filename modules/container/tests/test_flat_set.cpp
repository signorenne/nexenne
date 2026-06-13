/**
 * @file
 * @brief Tests for nexenne::container::flat_set.
 */

#include <doctest/doctest.h>

#include <functional>
#include <iterator>
#include <string>
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

}  // namespace
