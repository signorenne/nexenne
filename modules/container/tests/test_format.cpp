/**
 * @file
 * @brief Tests for nexenne::container format helpers.
 */

#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>

#include <nexenne/container/format.hpp>

namespace {

namespace cn = nexenne::container;
using namespace std::string_literals;

TEST_CASE("nexenne::container::format sequence containers print as [a, b, c]") {
  cn::static_vector<int, 8> v;
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  CHECK(cn::to_string(v) == "static_vector[1, 2, 3]");
  CHECK(std::format("{}", v) == "static_vector[1, 2, 3]");  // std::formatter path

  cn::static_vector<int, 8> const empty;
  CHECK(cn::to_string(empty) == "static_vector[]");
}

TEST_CASE("nexenne::container::format operator<< streams the same text") {
  cn::static_vector<int, 4> v;
  v.push_back(7);
  std::ostringstream os;
  os << v;
  CHECK(os.str() == "static_vector[7]");
}

TEST_CASE("nexenne::container::format bitset prints MSB-first binary") {
  cn::bitset_dynamic b(4);
  b.set(0);  // least significant
  b.set(3);  // most significant
  // bits printed MSB..LSB: bit3=1, bit2=0, bit1=0, bit0=1 -> 1001
  CHECK(cn::to_string(b) == "bitset_dynamic(size=4, bits=0b1001)");
}

TEST_CASE("nexenne::container::format set-like containers print as {a, b, c}") {
  cn::sparse_set<unsigned> s;
  s.insert(2);
  auto const str{cn::to_string(s)};
  CHECK(str == "sparse_set{2}");
}

TEST_CASE("nexenne::container::format map-like containers print key: value") {
  cn::flat_hash_map<int, int> m;
  m.insert(1, 10);
  // single entry keeps the output deterministic regardless of slot order
  CHECK(cn::to_string(m) == "flat_hash_map{1: 10}");
  CHECK(std::format("{}", m) == "flat_hash_map{1: 10}");
}

TEST_CASE("nexenne::container::format trie prints quoted keys") {
  cn::trie<char, int> t;
  t.insert("hi"s, 5);
  CHECK(cn::to_string(t) == "trie{\"hi\": 5}");
}

TEST_CASE("nexenne::container::format graph prints adjacency") {
  cn::graph<int> g{2};
  static_cast<void>(g.add_edge(0, 1, 9));
  CHECK(cn::to_string(g) == "graph{0:[1(9)], 1:[]}");
}

TEST_CASE("nexenne::container::format heap prints its backing layout") {
  cn::heap<int> h;
  h.push(5);
  h.push(1);
  // max-heap: 5 sits at the front of the layout
  auto const str{cn::to_string(h)};
  CHECK(str.starts_with("heap["));
  CHECK(str.find('5') != std::string::npos);
}

TEST_CASE("nexenne::container::format stable_vector and small_vector print as sequences") {
  cn::stable_vector<int> sv;
  sv.push_back(4);
  sv.push_back(5);
  CHECK(cn::to_string(sv) == "stable_vector[4, 5]");
  CHECK(std::format("{}", sv) == "stable_vector[4, 5]");
  cn::stable_vector<int> const empty_sv;
  CHECK(cn::to_string(empty_sv) == "stable_vector[]");

  cn::small_vector<int, 2> smv;
  smv.push_back(1);
  smv.push_back(2);
  smv.push_back(3);  // spills to heap
  CHECK(cn::to_string(smv) == "small_vector[1, 2, 3]");
  CHECK(std::format("{}", smv) == "small_vector[1, 2, 3]");
}

TEST_CASE("nexenne::container::format ring_buffer prints in FIFO order") {
  cn::ring_buffer<int, 4> r;
  static_cast<void>(r.push(1));
  static_cast<void>(r.push(2));
  static_cast<void>(r.push(3));
  CHECK(cn::to_string(r) == "ring_buffer[1, 2, 3]");
  CHECK(std::format("{}", r) == "ring_buffer[1, 2, 3]");

  cn::ring_buffer<int, 4> const empty;
  CHECK(cn::to_string(empty) == "ring_buffer[]");
}

TEST_CASE("nexenne::container::format bag prints as a sequence") {
  cn::bag<int> b;
  b.insert(7);
  // single element keeps output deterministic regardless of swap-removal layout
  CHECK(cn::to_string(b) == "bag[7]");
  CHECK(std::format("{}", b) == "bag[7]");

  cn::bag<int> const empty;
  CHECK(cn::to_string(empty) == "bag[]");
}

TEST_CASE("nexenne::container::format binary_tree prints in sorted order") {
  cn::binary_tree<int> t;
  t.insert(3);
  t.insert(1);
  t.insert(2);
  CHECK(cn::to_string(t) == "binary_tree{1, 2, 3}");  // in-order is sorted
  CHECK(std::format("{}", t) == "binary_tree{1, 2, 3}");

  cn::binary_tree<int> const empty;
  CHECK(cn::to_string(empty) == "binary_tree{}");
}

TEST_CASE("nexenne::container::format union_find groups members by root") {
  cn::union_find<unsigned> uf{4};
  static_cast<void>(uf.unite(0, 1));
  auto const str{cn::to_string(uf)};
  CHECK(str.starts_with("union_find["));
  CHECK(str.find("{0, 1}") != std::string::npos);  // 0 and 1 share a set

  cn::union_find<unsigned> const empty;
  CHECK(cn::to_string(empty) == "union_find{}");
}

TEST_CASE("nexenne::container::format dense_map prints key: value") {
  cn::dense_map<unsigned, int> m;
  static_cast<void>(m.insert(3u, 30));
  CHECK(cn::to_string(m) == "dense_map{3: 30}");
  CHECK(std::format("{}", m) == "dense_map{3: 30}");
}

TEST_CASE("nexenne::container::format flat_hash_set prints set-like") {
  cn::flat_hash_set<int> s;
  static_cast<void>(s.insert(5));
  CHECK(cn::to_string(s) == "flat_hash_set{5}");
  CHECK(std::format("{}", s) == "flat_hash_set{5}");

  cn::flat_hash_set<int> const empty;
  CHECK(cn::to_string(empty) == "flat_hash_set{}");
}

TEST_CASE("nexenne::container::format gap_buffer prints as a sequence") {
  cn::gap_buffer<int> b;
  b.insert(1);
  b.insert(2);
  CHECK(cn::to_string(b).starts_with("gap_buffer["));
  CHECK(std::format("{}", b).starts_with("gap_buffer["));
}

TEST_CASE("nexenne::container::format operator<< matches to_string across types") {
  cn::ring_buffer<int, 4> r;
  static_cast<void>(r.push(9));
  std::ostringstream os;
  os << r;
  CHECK(os.str() == cn::to_string(r));

  cn::binary_tree<int> t;
  t.insert(1);
  std::ostringstream ot;
  ot << t;
  CHECK(ot.str() == cn::to_string(t));
}

TEST_CASE("nexenne::container::format nested containers format recursively") {
  // A static_vector of static_vectors: the inner formatter must run per element.
  cn::static_vector<cn::static_vector<int, 4>, 4> outer;
  cn::static_vector<int, 4> a;
  a.push_back(1);
  a.push_back(2);
  cn::static_vector<int, 4> b;
  b.push_back(3);
  outer.push_back(a);
  outer.push_back(b);
  CHECK(cn::to_string(outer) == "static_vector[static_vector[1, 2], static_vector[3]]");
  CHECK(std::format("{}", outer) == "static_vector[static_vector[1, 2], static_vector[3]]");
}

TEST_CASE("nexenne::container::format trie with multiple entries lists each") {
  cn::trie<char, int> t;
  t.insert("a"s, 1);
  auto const str{cn::to_string(t)};
  CHECK(str == "trie{\"a\": 1}");
  CHECK(std::format("{}", t) == str);

  cn::trie<char, int> const empty;
  CHECK(cn::to_string(empty) == "trie{}");
}

}  // namespace
