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

}  // namespace
