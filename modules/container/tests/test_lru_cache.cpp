/**
 * @file
 * @brief Tests for nexenne::container::lru_cache.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <nexenne/container/lru_cache.hpp>

namespace {

namespace cn = nexenne::container;
template <std::size_t N>
using cache_t = cn::lru_cache<int, int, N>;

// A zero capacity is meaningless (a cache that can hold nothing) and is rejected
// at compile time by the requires (Capacity >= 1) constraint.
template <std::size_t N>
concept lru_cache_instantiable = requires { typename cn::lru_cache<int, int, N>; };
static_assert(lru_cache_instantiable<1>);
static_assert(lru_cache_instantiable<256>);
static_assert(!lru_cache_instantiable<0>);

TEST_CASE("nexenne::container::lru_cache put then get returns the value, promotes to MRU") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  CHECK(c.size() == 2);
  CHECK(c.full());
  REQUIRE(c.get(1) != nullptr);
  CHECK(*c.get(1) == 10);
  CHECK(c.get(99) == nullptr);  // miss
}

TEST_CASE("nexenne::container::lru_cache evicts the least recently used on a full put") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);            // recency MRU..LRU: 2, 1
  CHECK(*c.get(1) == 10);  // promote 1: now 1, 2
  c.put(3, 30);            // full: evict LRU (2)
  CHECK_FALSE(c.contains(2));
  CHECK(c.contains(1));
  CHECK(c.contains(3));
  CHECK(c.size() == 2);
}

TEST_CASE("nexenne::container::lru_cache put on an existing key updates value and promotes") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);  // MRU..LRU: 2, 1
  c.put(1, 11);  // update 1, promote it: 1, 2
  CHECK(*c.peek(1) == 11);
  c.put(3, 30);  // evict LRU (2), not 1
  CHECK(c.contains(1));
  CHECK_FALSE(c.contains(2));
}

TEST_CASE("nexenne::container::lru_cache peek does not promote") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);  // MRU..LRU: 2, 1
  REQUIRE(c.peek(1) != nullptr);
  CHECK(*c.peek(1) == 10);  // peek leaves recency untouched: still 2, 1
  c.put(3, 30);             // evict LRU (1)
  CHECK_FALSE(c.contains(1));
  CHECK(c.contains(2));
  CHECK(c.peek(99) == nullptr);
}

TEST_CASE("nexenne::container::lru_cache mru_key and lru_key track the ends") {
  cache_t<3> c{};
  CHECK(c.mru_key() == nullptr);  // empty
  CHECK(c.lru_key() == nullptr);
  c.put(1, 1);
  c.put(2, 2);
  c.put(3, 3);  // MRU..LRU: 3, 2, 1
  CHECK(*c.mru_key() == 3);
  CHECK(*c.lru_key() == 1);
  CHECK(*c.get(1) == 1);  // promote 1: 1, 3, 2
  CHECK(*c.mru_key() == 1);
  CHECK(*c.lru_key() == 2);
}

TEST_CASE("nexenne::container::lru_cache erase removes and recycles the slot") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  CHECK(c.erase(1));
  CHECK_FALSE(c.contains(1));
  CHECK(c.size() == 1);
  CHECK_FALSE(c.erase(1));  // already gone
  c.put(3, 30);             // reuses the freed slot, no eviction needed
  CHECK(c.contains(2));
  CHECK(c.contains(3));
  CHECK(c.size() == 2);
}

TEST_CASE("nexenne::container::lru_cache clear empties but keeps capacity") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  c.clear();
  CHECK(c.empty());
  CHECK(c.capacity() == 2);
  CHECK_FALSE(c.contains(1));
  c.put(5, 50);  // usable after clear
  CHECK(*c.get(5) == 50);
}

TEST_CASE("nexenne::container::lru_cache holds a move-only value") {
  cn::lru_cache<int, std::unique_ptr<int>, 2> c{};
  c.put(1, std::make_unique<int>(10));
  c.put(2, std::make_unique<int>(20));
  REQUIRE(c.get(1) != nullptr);
  CHECK(**c.get(1) == 10);
  c.put(3, std::make_unique<int>(30));  // evicts LRU (2), moves the new value in
  CHECK_FALSE(c.contains(2));
  CHECK(c.contains(1));
  CHECK(**c.get(3) == 30);
}

TEST_CASE("nexenne::container::lru_cache works with string keys") {
  cn::lru_cache<std::string, int, 2> c{};
  c.put("a", 1);
  c.put("b", 2);
  CHECK(*c.get("a") == 1);  // promote a
  c.put("c", 3);            // evict b
  CHECK(c.contains("a"));
  CHECK_FALSE(c.contains("b"));
  CHECK(c.contains("c"));
}

TEST_CASE("nexenne::container::lru_cache empty cache queries") {
  cache_t<2> c{};
  CHECK(c.empty());
  CHECK(c.size() == 0);
  CHECK_FALSE(c.full());
  CHECK(c.capacity() == 2);
  CHECK(c.max_size() == 2);
  CHECK(c.get(1) == nullptr);
  CHECK(c.peek(1) == nullptr);
  CHECK_FALSE(c.contains(1));
  CHECK_FALSE(c.erase(1));
  CHECK(c.mru_key() == nullptr);
  CHECK(c.lru_key() == nullptr);
}

TEST_CASE("nexenne::container::lru_cache capacity one evicts on every new put") {
  cache_t<1> c{};
  CHECK(c.capacity() == 1);
  c.put(1, 10);
  CHECK(c.full());
  CHECK(*c.mru_key() == 1);
  CHECK(*c.lru_key() == 1);  // sole entry is both ends
  c.put(2, 20);              // immediately evicts 1
  CHECK_FALSE(c.contains(1));
  CHECK(c.contains(2));
  CHECK(c.size() == 1);
  c.put(2, 21);  // updating the sole key does not evict
  CHECK(*c.peek(2) == 21);
  CHECK(c.size() == 1);
}

TEST_CASE("nexenne::container::lru_cache get miss leaves recency order untouched") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);  // MRU..LRU: 2, 1
  CHECK(c.get(99) == nullptr);
  CHECK(*c.mru_key() == 2);  // unchanged
  CHECK(*c.lru_key() == 1);
  c.put(3, 30);  // still evicts 1 (the genuine LRU)
  CHECK_FALSE(c.contains(1));
  CHECK(c.contains(2));
}

TEST_CASE("nexenne::container::lru_cache full put that updates an existing key never evicts") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);  // full: MRU..LRU 2, 1
  c.put(1, 11);  // existing key while full: update + promote, no eviction
  CHECK(c.size() == 2);
  CHECK(c.contains(1));
  CHECK(c.contains(2));
  CHECK(*c.peek(1) == 11);
  CHECK(*c.mru_key() == 1);
  CHECK(*c.lru_key() == 2);
}

TEST_CASE("nexenne::container::lru_cache erase then re-add stays consistent at the ends") {
  cache_t<3> c{};
  c.put(1, 1);
  c.put(2, 2);
  c.put(3, 3);  // MRU..LRU: 3, 2, 1
  CHECK(c.erase(2));
  CHECK(c.size() == 2);
  CHECK(*c.mru_key() == 3);
  CHECK(*c.lru_key() == 1);  // 2 removed from the middle, ends unchanged
  c.put(4, 4);               // free slot, no eviction: MRU..LRU 4, 3, 1
  CHECK(c.size() == 3);
  CHECK(c.contains(4));
  CHECK(*c.mru_key() == 4);
  CHECK(*c.lru_key() == 1);
}

// Known-sequence differential test: drive the cache through a fixed operation
// log and assert the eviction outcomes against a hand-computed reference.
TEST_CASE("nexenne::container::lru_cache hand-computed eviction sequence") {
  cache_t<3> c{};
  // recency lists written MRU..LRU after each step.
  c.put(1, 1);            // [1]
  c.put(2, 2);            // [2, 1]
  c.put(3, 3);            // [3, 2, 1]  full
  CHECK(*c.get(1) == 1);  // [1, 3, 2]  get promotes 1
  c.put(4, 4);            // [4, 1, 3]  evict LRU=2
  CHECK_FALSE(c.contains(2));
  CHECK(*c.mru_key() == 4);
  CHECK(*c.lru_key() == 3);
  c.put(3, 33);  // [3, 4, 1]  existing key: update + promote, no eviction
  CHECK(*c.peek(3) == 33);
  CHECK(c.size() == 3);
  c.put(5, 5);  // [5, 3, 4]  evict LRU=1
  CHECK_FALSE(c.contains(1));
  CHECK(c.contains(3));
  CHECK(c.contains(4));
  CHECK(c.contains(5));
  CHECK(*c.mru_key() == 5);
  CHECK(*c.lru_key() == 4);
  c.put(6, 6);  // [6, 5, 3]  evict LRU=4
  CHECK_FALSE(c.contains(4));
  CHECK(*c.lru_key() == 3);
  CHECK(c.size() == 3);
}

// A second differential run with string keys under the sanitizers.
TEST_CASE("nexenne::container::lru_cache string-key eviction sequence is exact") {
  cn::lru_cache<std::string, int, 2> c{};
  c.put("a", 1);  // [a]
  c.put("b", 2);  // [b, a]
  c.put("c", 3);  // [c, b]   evict a
  CHECK_FALSE(c.contains("a"));
  CHECK(*c.peek("b") == 2);  // peek does not promote: [c, b]
  c.put("d", 4);             // [d, c]   evict b (still LRU)
  CHECK_FALSE(c.contains("b"));
  CHECK(c.contains("c"));
  CHECK(c.contains("d"));
  CHECK(*c.mru_key() == "d");
  CHECK(*c.lru_key() == "c");
}

TEST_CASE("nexenne::container::lru_cache mru_key and lru_key after put updates") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);  // MRU..LRU: 2, 1
  CHECK(*c.mru_key() == 2);
  c.put(1, 11);  // update + promote 1: 1, 2
  CHECK(*c.mru_key() == 1);
  CHECK(*c.lru_key() == 2);
  CHECK(c.erase(1));  // removing MRU
  CHECK(*c.mru_key() == 2);
  CHECK(*c.lru_key() == 2);
  CHECK(c.erase(2));  // removing the last entry
  CHECK(c.mru_key() == nullptr);
  CHECK(c.lru_key() == nullptr);
  CHECK(c.empty());
}

}  // namespace
