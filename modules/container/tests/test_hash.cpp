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

TEST_CASE("nexenne::container::hash static_vector is deterministic and handles empty") {
  cn::static_vector<int, 8> v;
  v.push_back(10);
  v.push_back(20);
  std::hash<cn::static_vector<int, 8>> const h;
  CHECK(h(v) == h(v));  // same object hashes identically every call

  cn::static_vector<int, 8> const e1;
  cn::static_vector<int, 8> const e2;
  CHECK(h(e1) == h(e2));  // two empty vectors agree

  // Order matters for a sequence hash: [1,2] should not collide with [2,1].
  cn::static_vector<int, 8> ab;
  ab.push_back(1);
  ab.push_back(2);
  cn::static_vector<int, 8> ba;
  ba.push_back(2);
  ba.push_back(1);
  CHECK(ab != ba);
  CHECK(h(ab) != h(ba));
}

TEST_CASE("nexenne::container::hash stable_vector equal vectors hash equal") {
  cn::stable_vector<int> a;
  a.push_back(4);
  a.push_back(5);
  a.push_back(6);
  cn::stable_vector<int> b;
  b.push_back(4);
  b.push_back(5);
  b.push_back(6);
  std::hash<cn::stable_vector<int>> const h;
  CHECK(a == b);
  CHECK(h(a) == h(b));

  cn::stable_vector<int> shorter;
  shorter.push_back(4);
  shorter.push_back(5);
  CHECK(a != shorter);
  CHECK(h(a) != h(shorter));  // different length, almost certainly differs

  std::unordered_set<cn::stable_vector<int>> seen;
  seen.insert(a);
  CHECK(seen.contains(b));
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

  cn::bitset_dynamic different(10);
  different.set(2);
  different.set(8);  // 8 instead of 7
  CHECK(a != different);
  CHECK(h(a) != h(different));  // different bits, almost certainly differs

  cn::bitset_dynamic const empty1(0);
  cn::bitset_dynamic const empty2(0);
  CHECK(h(empty1) == h(empty2));  // empty bitsets agree

  // Same bits but different declared size must not collide (size is folded in).
  cn::bitset_dynamic small(4);
  small.set(1);
  cn::bitset_dynamic large(64);
  large.set(1);
  CHECK(small != large);
  CHECK(h(small) != h(large));
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

  cn::binary_tree<int> different;
  different.insert(5);
  different.insert(2);
  different.insert(9);  // 9 instead of 8
  CHECK(a != different);
  CHECK(h(a) != h(different));

  cn::binary_tree<int> const e1;
  cn::binary_tree<int> const e2;
  CHECK(h(e1) == h(e2));  // empty trees agree
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
  CHECK(h(a) == h(a));  // deterministic

  cn::ring_buffer<int, 4> reversed;
  static_cast<void>(reversed.push(2));
  static_cast<void>(reversed.push(1));  // same elements, different FIFO order
  CHECK(h(a) != h(reversed));

  cn::ring_buffer<int, 4> const empty1;
  cn::ring_buffer<int, 4> const empty2;
  CHECK(h(empty1) == h(empty2));  // empty buffers agree
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

  cn::trie<char, int> different;
  different.insert("cat"s, 1);
  different.insert("car"s, 3);  // same keys, one value differs
  CHECK(a != different);
  CHECK(h(a) != h(different));

  cn::trie<char, int> const e1;
  cn::trie<char, int> const e2;
  CHECK(h(e1) == h(e2));  // empty tries agree

  std::unordered_set<cn::trie<char, int>> seen;
  seen.insert(a);
  CHECK(seen.contains(b));  // b == a is found by hash + equality
  CHECK_FALSE(seen.contains(different));
}

}  // namespace
