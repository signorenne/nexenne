/**
 * @file
 * @brief Tests for nexenne::container::binary_tree.
 */

#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <random>
#include <set>
#include <string>
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

TEST_CASE("nexenne::container::binary_tree the empty tree") {
  tree_t t;
  CHECK(t.empty());
  CHECK(t.size() == 0);
  CHECK(t.begin() == t.end());
  CHECK(t.cbegin() == t.cend());
  CHECK(t.find(0) == nullptr);
  CHECK_FALSE(t.contains(0));
  CHECK_FALSE(t.erase(0));
  CHECK(to_vector(t).empty());
  CHECK(tree_t::max_size() > 0);
}

TEST_CASE("nexenne::container::binary_tree single-element tree") {
  tree_t t;
  CHECK(t.insert(42));
  CHECK(t.size() == 1);
  CHECK_FALSE(t.empty());
  REQUIRE(t.find(42) != nullptr);
  CHECK(*t.begin() == 42);
  CHECK(std::next(t.begin()) == t.end());
  CHECK(t.erase(42));
  CHECK(t.empty());
  CHECK(t.begin() == t.end());
}

TEST_CASE("nexenne::container::binary_tree clear empties a populated tree") {
  tree_t t;
  for (int v : {5, 3, 8, 1, 9}) {
    t.insert(v);
  }
  t.clear();
  CHECK(t.empty());
  CHECK(t.size() == 0);
  CHECK(t.begin() == t.end());
  t.insert(7);  // usable after clear
  CHECK(to_vector(t) == std::vector{7});
}

TEST_CASE("nexenne::container::binary_tree degenerate sorted-input chain stays a valid BST") {
  tree_t t;
  for (int v{0}; v < 32; ++v) {  // strictly ascending: a right-leaning chain
    CHECK(t.insert(v));
  }
  CHECK(t.size() == 32);
  std::vector<int> expected;
  for (int v{0}; v < 32; ++v) {
    expected.push_back(v);
  }
  CHECK(to_vector(t) == expected);
  // erase from the middle of the chain keeps order
  CHECK(t.erase(16));
  CHECK_FALSE(t.contains(16));
  CHECK(t.contains(15));
  CHECK(t.contains(17));
}

TEST_CASE("nexenne::container::binary_tree erase a node whose successor is its right child") {
  // Node with two children whose right child has no left subtree: the immediate
  // right child becomes the replacement (y_parent == z branch).
  tree_t t;
  for (int v : {5, 3, 8, 9}) {  // 5 has children 3 and 8; 8 has only right child 9
    t.insert(v);
  }
  CHECK(t.erase(5));
  CHECK(to_vector(t) == std::vector{3, 8, 9});
  CHECK(t.size() == 3);
}

TEST_CASE("nexenne::container::binary_tree move assignment steals the graph") {
  tree_t a;
  a.insert(1);
  a.insert(2);
  tree_t b;
  b.insert(99);
  b = std::move(a);
  CHECK(to_vector(b) == std::vector{1, 2});
  CHECK(a.empty());  // NOLINT: checking moved-from state is intentional
}

TEST_CASE("nexenne::container::binary_tree self move-assignment leaves it unchanged") {
  tree_t t;
  t.insert(1);
  t.insert(2);
  auto& alias{t};
  t = std::move(alias);  // NOLINT: deliberate self-move
  CHECK(to_vector(t) == std::vector{1, 2});
}

TEST_CASE("nexenne::container::binary_tree copy assignment deep-clones") {
  tree_t a;
  a.insert(2);
  a.insert(1);
  a.insert(3);
  tree_t b;
  b.insert(99);  // pre-existing content is replaced
  b = a;
  CHECK(a == b);
  b.insert(4);
  CHECK(to_vector(a) == std::vector{1, 2, 3});  // a unchanged
  CHECK(to_vector(b) == std::vector{1, 2, 3, 4});
}

TEST_CASE("nexenne::container::binary_tree self copy-assignment leaves it unchanged") {
  tree_t t;
  t.insert(1);
  t.insert(2);
  auto const& alias{t};
  t = alias;  // NOLINT: deliberate self-copy
  CHECK(to_vector(t) == std::vector{1, 2});
}

TEST_CASE("nexenne::container::binary_tree swap exchanges state") {
  tree_t a;
  a.insert(1);
  a.insert(2);
  tree_t b;
  b.insert(9);
  a.swap(b);
  CHECK(to_vector(a) == std::vector{9});
  CHECK(to_vector(b) == std::vector{1, 2});
  swap(a, b);  // friend swap
  CHECK(to_vector(a) == std::vector{1, 2});
  CHECK(to_vector(b) == std::vector{9});
}

TEST_CASE("nexenne::container::binary_tree post-increment iterator and const traversal") {
  tree_t t;
  for (int v : {3, 1, 2}) {
    t.insert(v);
  }
  auto it{t.begin()};
  auto const copy{it++};
  CHECK(*copy == 1);
  CHECK(*it == 2);
  // const overloads of begin/end via a const reference
  tree_t const& ct{t};
  std::vector<int> out;
  for (auto i{ct.begin()}; i != ct.end(); ++i) {
    out.push_back(*i);
  }
  CHECK(out == std::vector{1, 2, 3});
  CHECK(ct.find(2) != nullptr);  // const find overload
  CHECK(ct.find(99) == nullptr);
}

TEST_CASE(
  "nexenne::container::binary_tree non-const find allows in-place mutation of a non-key payload"
) {
  // Mutating through find on an int *is* the key, so use a tree whose order
  // ignores part of the value: a pair compared by .first only.
  using kv = std::pair<int, int>;

  struct by_first {
    auto operator()(kv const& a, kv const& b) const noexcept -> bool {
      return a.first < b.first;
    }
  };

  cn::binary_tree<kv, by_first> t;
  t.insert(kv{1, 10});
  t.insert(kv{2, 20});
  auto* const p{t.find(kv{2, 0})};  // located by key 2
  REQUIRE(p != nullptr);
  p->second = 99;  // mutate the non-ordering payload in place
  CHECK(t.find(kv{2, 0})->second == 99);
}

TEST_CASE("nexenne::container::binary_tree of non-trivial std::string values") {
  cn::binary_tree<std::string> t;
  CHECK(t.insert(std::string("banana")));
  CHECK(t.insert(std::string("apple")));
  CHECK(t.insert(std::string("cherry")));
  CHECK_FALSE(t.insert(std::string("apple")));  // duplicate
  CHECK(t.size() == 3);
  std::vector<std::string> out;
  for (auto const& s : t) {
    out.push_back(s);
  }
  CHECK(out == std::vector<std::string>{"apple", "banana", "cherry"});
  REQUIRE(t.find(std::string("banana")) != nullptr);
  CHECK(*t.find(std::string("banana")) == "banana");
  CHECK(t.erase(std::string("banana")));
  CHECK_FALSE(t.contains(std::string("banana")));
  // copy a string tree and confirm independence under LSan
  cn::binary_tree<std::string> clone{t};
  t.clear();
  CHECK(clone.contains(std::string("apple")));
}

TEST_CASE("nexenne::container::binary_tree custom-comparator constructor is honoured") {
  cn::binary_tree<int, std::greater<int>> t{std::greater<int>{}};
  for (int v : {1, 5, 3}) {
    t.insert(v);
  }
  CHECK(to_vector(t) == std::vector{5, 3, 1});
}

TEST_CASE("nexenne::container::binary_tree differential against std::set under random ops") {
  std::mt19937 rng{1234};
  std::uniform_int_distribution<int> values{0, 99};
  std::uniform_int_distribution<int> ops{0, 2};
  tree_t t;
  std::set<int> ref;
  for (int step{0}; step < 4000; ++step) {
    int const v{values(rng)};
    switch (ops(rng)) {
      case 0: {
        bool const a{t.insert(v)};
        bool const b{ref.insert(v).second};
        CHECK(a == b);
        break;
      }
      case 1: {
        bool const a{t.erase(v)};
        bool const b{ref.erase(v) == 1};
        CHECK(a == b);
        break;
      }
      default:
        CHECK(t.contains(v) == (ref.count(v) == 1));
        break;
    }
    CHECK(t.size() == ref.size());
  }
  // in-order traversal must match the sorted reference exactly
  CHECK(to_vector(t) == std::vector<int>(ref.begin(), ref.end()));
}

}  // namespace
