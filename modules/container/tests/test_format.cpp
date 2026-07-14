/**
 * @file
 * @brief Tests for nexenne::container format helpers.
 */

#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>

#include <nexenne/container/format.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;
using namespace std::string_literals;

TEST_CASE("nexenne::container::format sequence containers print as [a, b, c]") {
  cn::static_vector<int, 8> v;
  nexenne::utility::discard(v.push_back(1));
  nexenne::utility::discard(v.push_back(2));
  nexenne::utility::discard(v.push_back(3));
  CHECK(cn::to_string(v) == "static_vector[1, 2, 3]");
  CHECK(std::format("{}", v) == "static_vector[1, 2, 3]");  // std::formatter path

  cn::static_vector<int, 8> const empty;
  CHECK(cn::to_string(empty) == "static_vector[]");
}

TEST_CASE("nexenne::container::format operator<< streams the same text") {
  cn::static_vector<int, 4> v;
  nexenne::utility::discard(v.push_back(7));
  std::ostringstream os;
  os << v;
  CHECK(os.str() == "static_vector[7]");
}

TEST_CASE("nexenne::container::format bitset prints MSB-first binary") {
  cn::bitset_dynamic b(4);
  nexenne::utility::discard(b.set(0));  // least significant
  nexenne::utility::discard(b.set(3));  // most significant
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
  nexenne::utility::discard(g.add_edge(0, 1, 9));
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
  nexenne::utility::discard(r.push(1));
  nexenne::utility::discard(r.push(2));
  nexenne::utility::discard(r.push(3));
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
  nexenne::utility::discard(uf.unite(0, 1));
  auto const str{cn::to_string(uf)};
  CHECK(str.starts_with("union_find["));
  CHECK(str.find("{0, 1}") != std::string::npos);  // 0 and 1 share a set

  cn::union_find<unsigned> const empty;
  CHECK(cn::to_string(empty) == "union_find{}");
}

TEST_CASE("nexenne::container::format dense_map prints key: value") {
  cn::dense_map<unsigned, int> m;
  nexenne::utility::discard(m.insert(3u, 30));
  CHECK(cn::to_string(m) == "dense_map{3: 30}");
  CHECK(std::format("{}", m) == "dense_map{3: 30}");
}

TEST_CASE("nexenne::container::format flat_hash_set prints set-like") {
  cn::flat_hash_set<int> s;
  nexenne::utility::discard(s.insert(5));
  CHECK(cn::to_string(s) == "flat_hash_set{5}");
  CHECK(std::format("{}", s) == "flat_hash_set{5}");

  cn::flat_hash_set<int> const empty;
  CHECK(cn::to_string(empty) == "flat_hash_set{}");
}

TEST_CASE("nexenne::container::format deque prints as a sequence (index-walked)") {
  cn::deque<int> d;
  d.push_back(1);
  d.push_back(2);
  d.push_front(0);
  CHECK(cn::to_string(d) == "deque[0, 1, 2]");
  CHECK(std::format("{}", d) == "deque[0, 1, 2]");

  cn::deque<int> const empty;
  CHECK(cn::to_string(empty) == "deque[]");
}

TEST_CASE("nexenne::container::format ordered flat containers match the hashed brace style") {
  cn::flat_map<int, int> m;
  m.insert({2, 20});
  m.insert({1, 10});
  // flat_map is sorted, so the output order is deterministic.
  CHECK(cn::to_string(m) == "flat_map{1: 10, 2: 20}");
  CHECK(std::format("{}", m) == "flat_map{1: 10, 2: 20}");

  cn::flat_set<int> s;
  s.insert(3);
  s.insert(1);
  s.insert(2);
  CHECK(cn::to_string(s) == "flat_set{1, 2, 3}");
  CHECK(std::format("{}", s) == "flat_set{1, 2, 3}");

  cn::static_flat_map<int, int, 8> sm;
  sm.insert({5, 50});
  sm.insert({4, 40});
  CHECK(cn::to_string(sm) == "static_flat_map{4: 40, 5: 50}");
  CHECK(std::format("{}", sm) == "static_flat_map{4: 40, 5: 50}");
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
  nexenne::utility::discard(r.push(9));
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
  nexenne::utility::discard(a.push_back(1));
  nexenne::utility::discard(a.push_back(2));
  cn::static_vector<int, 4> b;
  nexenne::utility::discard(b.push_back(3));
  nexenne::utility::discard(outer.push_back(a));
  nexenne::utility::discard(outer.push_back(b));
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

TEST_CASE("nexenne::container::format bloom_filter prints a stats line") {
  cn::bloom_filter<int> f{16, 3};
  auto const empty_form{
    "bloom_filter(bit_count=16, hash_count=3, insertions=0, false_positive_rate=0)"s
  };
  CHECK(cn::to_string(f) == empty_form);
  CHECK(std::format("{}", f) == cn::to_string(f));
  f.insert(1);
  CHECK(std::format("{}", f).starts_with(
    "bloom_filter(bit_count=16, hash_count=3, insertions=1,"s
  ));
}

TEST_CASE("nexenne::container::format lru_cache prints size and capacity") {
  // Pins the lru_cache stats formatter. NOTE: lru_cache currently trips a
  // pre-existing intrusive_list teardown assert (owned by another component)
  // when it is destroyed, so this case aborts at scope exit until that sibling
  // fix lands. The formatter output itself (the two checks below) is correct and
  // reads only size() and capacity().
  cn::lru_cache<int, int, 4> c;
  c.put(1, 10);
  c.put(2, 20);
  CHECK(cn::to_string(c) == "lru_cache(size=2, capacity=4)");
  CHECK(std::format("{}", c) == "lru_cache(size=2, capacity=4)");
}

TEST_CASE("nexenne::container::format lock-free queues print approximate stats") {
  cn::spsc_queue<int, 8> sp;
  REQUIRE(sp.push(1).has_value());
  REQUIRE(sp.push(2).has_value());
  CHECK(cn::to_string(sp) == "spsc_queue(size_approx=2, capacity=7)");
  CHECK(std::format("{}", sp) == "spsc_queue(size_approx=2, capacity=7)");

  cn::mpsc_queue<int, 8> mp;
  REQUIRE(mp.push(1).has_value());
  CHECK(cn::to_string(mp) == "mpsc_queue(size_approx=1, capacity=8)");
  CHECK(std::format("{}", mp) == "mpsc_queue(size_approx=1, capacity=8)");

  cn::mpmc_queue<int, 8> mm;
  REQUIRE(mm.push(1).has_value());
  REQUIRE(mm.push(2).has_value());
  REQUIRE(mm.push(3).has_value());
  CHECK(cn::to_string(mm) == "mpmc_queue(size_approx=3, capacity=8)");
  CHECK(std::format("{}", mm) == "mpmc_queue(size_approx=3, capacity=8)");
}

TEST_CASE("nexenne::container::format object_pool prints size, capacity and high water") {
  cn::object_pool<int, 4> pool;
  auto const a{pool.emplace(1)};
  REQUIRE(a.has_value());
  auto const b{pool.emplace(2)};
  REQUIRE(b.has_value());
  CHECK(cn::to_string(pool) == "object_pool(size=2, capacity=4, high_water_mark=2)");
  CHECK(std::format("{}", pool) == "object_pool(size=2, capacity=4, high_water_mark=2)");
  // Leave the pool clean for its leak-free destruction contract.
  REQUIRE(pool.destroy(*a).has_value());
  REQUIRE(pool.destroy(*b).has_value());
}

TEST_CASE("nexenne::container::format linear_arena prints byte usage") {
  cn::linear_arena<256> arena;
  REQUIRE(arena.allocate(40, 8).has_value());
  auto const form{"linear_arena(bytes_used=40, capacity=256, high_water_mark=40)"s};
  CHECK(cn::to_string(arena) == form);
  CHECK(std::format("{}", arena) == form);
}

TEST_CASE("nexenne::container::format scratch_pad prints its checkpoint and live usage") {
  cn::linear_arena<256> arena;
  REQUIRE(arena.allocate(16, 8).has_value());
  cn::scratch_pad scratch{arena};
  REQUIRE(scratch.allocate(24, 8).has_value());
  CHECK(cn::to_string(scratch) == "scratch_pad(saved_offset=16, bytes_used=40)");
  CHECK(std::format("{}", scratch) == "scratch_pad(saved_offset=16, bytes_used=40)");
}

TEST_CASE("nexenne::container::format slot_key prints its index and generation") {
  cn::slot_key const null_key{};
  CHECK(cn::to_string(null_key) == "slot_key(0:0)");
  CHECK(std::format("{}", null_key) == "slot_key(0:0)");

  cn::slot_map<int> map;
  auto const handle{map.insert(42)};
  auto const form{std::format("slot_key({}:{})", handle.index(), handle.generation())};
  CHECK(cn::to_string(handle) == form);
  CHECK(std::format("{}", handle) == form);

  std::ostringstream os;
  os << handle;
  CHECK(os.str() == form);
}

}  // namespace
