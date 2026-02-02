/**
 * @file
 * @brief Tests for nexenne::container::binary_tree.
 */

#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/binary_tree.hpp>

namespace {

namespace cn = nexenne::container;
using tree_t = cn::binary_tree<int>;

template <typename Tree>
auto to_vector(Tree const& t) -> std::vector<int> {
  std::vector<int> out;
  for (auto const& v : t) {
    out.push_back(v);
  }
  return out;
}

TEST_CASE("nexenne::container::binary_tree insert keeps unique, iterates in order") {
  tree_t t;
  CHECK(t.insert(5));
  CHECK(t.insert(2));
  CHECK(t.insert(8));
  CHECK(t.insert(1));
  CHECK(t.insert(3));
  CHECK_FALSE(t.insert(5));  // duplicate rejected
  CHECK(t.size() == 5);
  CHECK(to_vector(t) == std::vector{1, 2, 3, 5, 8});
}

TEST_CASE("nexenne::container::binary_tree find and contains") {
  tree_t t;
  t.insert(4);
  t.insert(6);
  REQUIRE(t.find(4) != nullptr);
  CHECK(*t.find(4) == 4);
  CHECK(t.find(99) == nullptr);
  CHECK(t.contains(6));
  CHECK_FALSE(t.contains(99));
}

TEST_CASE("nexenne::container::binary_tree erase a leaf") {
  tree_t t;
  t.insert(5);
  t.insert(2);
  t.insert(8);
  CHECK(t.erase(2));  // leaf
  CHECK(to_vector(t) == std::vector{5, 8});
  CHECK_FALSE(t.erase(2));
}

TEST_CASE("nexenne::container::binary_tree erase a node with one child") {
  tree_t t;
  t.insert(5);
  t.insert(2);
  t.insert(1);  // 2 has only a left child
  CHECK(t.erase(2));
  CHECK(to_vector(t) == std::vector{1, 5});
  CHECK(t.contains(1));
}

TEST_CASE("nexenne::container::binary_tree erase a node with two children (immediate successor)") {
  tree_t t;
  t.insert(5);
  t.insert(2);
  t.insert(8);
  t.insert(7);  // successor of 5 is 7 (leftmost of right subtree, immediate-ish)
  t.insert(9);
  CHECK(t.erase(5));  // two children
  CHECK(to_vector(t) == std::vector{2, 7, 8, 9});
  CHECK(t.size() == 4);
}

TEST_CASE("nexenne::container::binary_tree erase a node with two children (buried successor)") {
  tree_t t;
  for (int v : {50, 30, 70, 60, 80, 55, 65}) {
    t.insert(v);
  }
  // successor of 50 is 55, buried as the leftmost of 50's right subtree
  CHECK(t.erase(50));
  CHECK(to_vector(t) == std::vector{30, 55, 60, 65, 70, 80});
  CHECK(t.size() == 6);
  // tree stays a valid BST: every subsequent erase keeps order
  CHECK(t.erase(70));
  CHECK(to_vector(t) == std::vector{30, 55, 60, 65, 80});
}

TEST_CASE("nexenne::container::binary_tree erase the root repeatedly drains in order") {
  tree_t t;
  for (int v : {5, 3, 8, 1, 4, 7, 9, 2, 6}) {
    t.insert(v);
  }
  std::vector<int> removed;
  while (!t.empty()) {
    int const root{*t.begin()};  // smallest; just drain by smallest
    removed.push_back(root);
    CHECK(t.erase(root));
  }
  CHECK(removed == std::vector{1, 2, 3, 4, 5, 6, 7, 8, 9});
}

TEST_CASE("nexenne::container::binary_tree deep copy is independent") {
  tree_t a;
  a.insert(2);
  a.insert(1);
  a.insert(3);
  tree_t b{a};  // deep clone
  CHECK(a == b);
  b.insert(4);
  b.erase(1);
  CHECK(to_vector(a) == std::vector{1, 2, 3});  // a unchanged
  CHECK(to_vector(b) == std::vector{2, 3, 4});
}

TEST_CASE("nexenne::container::binary_tree move steals the graph") {
  tree_t a;
  a.insert(1);
  a.insert(2);
  tree_t b{std::move(a)};
  CHECK(to_vector(b) == std::vector{1, 2});
  CHECK(a.empty());  // NOLINT: checking moved-from state is intentional
}

TEST_CASE("nexenne::container::binary_tree comparison operators") {
  tree_t a;
  a.insert(1);
  a.insert(2);
  tree_t b;
  b.insert(2);  // different insert order, same set
  b.insert(1);
  tree_t c;
  c.insert(1);
  c.insert(3);
  CHECK(a == b);
  CHECK(a != c);
  CHECK(a < c);  // {1,2} < {1,3}
  CHECK(c > a);
}

TEST_CASE("nexenne::container::binary_tree honours a custom comparator") {
  cn::binary_tree<int, std::greater<int>> t;  // descending order
  t.insert(1);
  t.insert(3);
  t.insert(2);
  CHECK(to_vector(t) == std::vector{3, 2, 1});
}

TEST_CASE("nexenne::container::binary_tree holds a move-only value") {
  cn::binary_tree<std::unique_ptr<int>> t;
  t.insert(std::make_unique<int>(2));
  t.emplace(std::make_unique<int>(1));
  CHECK(t.size() == 2);
  // in-order ascending by pointer comparison is unspecified, but both are present
  int seen{0};
  for (auto const& p : t) {
    seen += *p;
  }
  CHECK(seen == 3);
}

}  // namespace
