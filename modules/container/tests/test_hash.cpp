/**
 * @file
 * @brief Tests for nexenne::container hash specializations.
 */

#include <doctest/doctest.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

#include <nexenne/container/hash.hpp>

namespace {

namespace cn = nexenne::container;
using namespace std::string_literals;

TEST_CASE("nexenne::container::hash static_vector equal vectors hash equal") {
  cn::static_vector<int, 8> a;
  a.push_back(1);
  a.push_back(2);
  a.push_back(3);
  cn::static_vector<int, 8> b{a};
  cn::static_vector<int, 8> c;
  c.push_back(1);
  c.push_back(2);

  std::hash<cn::static_vector<int, 8>> const h;
  CHECK(h(a) == h(b));  // equal contents hash equal
  CHECK(h(a) != h(c));  // different length, almost certainly different hash

  std::unordered_set<cn::static_vector<int, 8>> seen;
  seen.insert(a);
  CHECK(seen.contains(b));  // b == a, so it is found
  CHECK_FALSE(seen.contains(c));
}

TEST_CASE("nexenne::container::hash small_vector works as a map key") {
  std::unordered_map<cn::small_vector<int, 2>, std::string> m;
  cn::small_vector<int, 2> key;
  key.push_back(7);
  key.push_back(8);
  key.push_back(9);  // spills to heap
  m[key] = "found";
  cn::small_vector<int, 2> probe;
  probe.push_back(7);
  probe.push_back(8);
  probe.push_back(9);
  REQUIRE(m.contains(probe));
  CHECK(m[probe] == "found");
}

TEST_CASE("nexenne::container::hash bitset_dynamic equal bitsets hash equal") {
  cn::bitset_dynamic a(10);
  a.set(2);
  a.set(7);
  cn::bitset_dynamic b(10);
  b.set(7);
  b.set(2);  // set in a different order, same bits
  std::hash<cn::bitset_dynamic> const h;
  CHECK(a == b);
  CHECK(h(a) == h(b));
}

TEST_CASE("nexenne::container::hash binary_tree equal trees hash equal") {
  cn::binary_tree<int> a;
  a.insert(5);
  a.insert(2);
  a.insert(8);
  cn::binary_tree<int> b;
  b.insert(8);  // different insert order, same set
  b.insert(5);
  b.insert(2);
  std::hash<cn::binary_tree<int>> const h;
  CHECK(a == b);
  CHECK(h(a) == h(b));  // in-order traversal is canonical
}

TEST_CASE("nexenne::container::hash ring_buffer hashes in FIFO order") {
  cn::ring_buffer<int, 4> a;
  static_cast<void>(a.push(1));
  static_cast<void>(a.push(2));
  cn::ring_buffer<int, 4> b;
  static_cast<void>(b.push(1));
  static_cast<void>(b.push(2));
  std::hash<cn::ring_buffer<int, 4>> const h;
  CHECK(h(a) == h(b));
}

TEST_CASE("nexenne::container::hash trie equal tries hash equal regardless of insert order") {
  cn::trie<char, int> a;
  a.insert("cat"s, 1);
  a.insert("car"s, 2);
  cn::trie<char, int> b;
  b.insert("car"s, 2);  // inserted in a different order
  b.insert("cat"s, 1);
  std::hash<cn::trie<char, int>> const h;
  CHECK(a == b);
  CHECK(h(a) == h(b));
}

}  // namespace
