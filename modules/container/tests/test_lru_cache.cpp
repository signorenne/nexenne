/**
 * @file
 * @brief Tests for nexenne::container::lru_cache.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <nexenne/container/lru_cache.hpp>

namespace {

namespace cn = nexenne::container;
template <std::size_t N>
using cache_t = cn::lru_cache<int, int, N>;

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
  CHECK(c.get(99) == nullptr);
}

TEST_CASE("nexenne::container::lru_cache evicts the least recently used on a full put") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  CHECK(*c.get(1) == 10);
  c.put(3, 30);
  CHECK_FALSE(c.contains(2));
  CHECK(c.contains(1));
  CHECK(c.contains(3));
  CHECK(c.size() == 2);
}

TEST_CASE("nexenne::container::lru_cache put on an existing key updates value and promotes") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  c.put(1, 11);
  CHECK(*c.peek(1) == 11);
  c.put(3, 30);
  CHECK(c.contains(1));
  CHECK_FALSE(c.contains(2));
}

TEST_CASE("nexenne::container::lru_cache peek does not promote") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  REQUIRE(c.peek(1) != nullptr);
  CHECK(*c.peek(1) == 10);
  c.put(3, 30);
  CHECK_FALSE(c.contains(1));
  CHECK(c.contains(2));
  CHECK(c.peek(99) == nullptr);
}

TEST_CASE("nexenne::container::lru_cache mru_key and lru_key track the ends") {
  cache_t<3> c{};
  CHECK(c.mru_key() == nullptr);
  CHECK(c.lru_key() == nullptr);
  c.put(1, 1);
  c.put(2, 2);
  c.put(3, 3);
  CHECK(*c.mru_key() == 3);
  CHECK(*c.lru_key() == 1);
  CHECK(*c.get(1) == 1);
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
  CHECK_FALSE(c.erase(1));
  c.put(3, 30);
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
  c.put(5, 50);
  CHECK(*c.get(5) == 50);
}

TEST_CASE("nexenne::container::lru_cache holds a move-only value") {
  cn::lru_cache<int, std::unique_ptr<int>, 2> c{};
  c.put(1, std::make_unique<int>(10));
  c.put(2, std::make_unique<int>(20));
  REQUIRE(c.get(1) != nullptr);
  CHECK(**c.get(1) == 10);
  c.put(3, std::make_unique<int>(30));
  CHECK_FALSE(c.contains(2));
  CHECK(c.contains(1));
  CHECK(**c.get(3) == 30);
}

TEST_CASE("nexenne::container::lru_cache works with string keys") {
  cn::lru_cache<std::string, int, 2> c{};
  c.put("a", 1);
  c.put("b", 2);
  CHECK(*c.get("a") == 1);
  c.put("c", 3);
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
  CHECK(*c.lru_key() == 1);
  c.put(2, 20);
  CHECK_FALSE(c.contains(1));
  CHECK(c.contains(2));
  CHECK(c.size() == 1);
  c.put(2, 21);
  CHECK(*c.peek(2) == 21);
  CHECK(c.size() == 1);
}

TEST_CASE("nexenne::container::lru_cache get miss leaves recency order untouched") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  CHECK(c.get(99) == nullptr);
  CHECK(*c.mru_key() == 2);
  CHECK(*c.lru_key() == 1);
  c.put(3, 30);
  CHECK_FALSE(c.contains(1));
  CHECK(c.contains(2));
}

TEST_CASE("nexenne::container::lru_cache full put that updates an existing key never evicts") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  c.put(1, 11);
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
  c.put(3, 3);
  CHECK(c.erase(2));
  CHECK(c.size() == 2);
  CHECK(*c.mru_key() == 3);
  CHECK(*c.lru_key() == 1);
  c.put(4, 4);
  CHECK(c.size() == 3);
  CHECK(c.contains(4));
  CHECK(*c.mru_key() == 4);
  CHECK(*c.lru_key() == 1);
}

TEST_CASE("nexenne::container::lru_cache hand-computed eviction sequence") {
  cache_t<3> c{};
  c.put(1, 1);
  c.put(2, 2);
  c.put(3, 3);
  CHECK(*c.get(1) == 1);
  c.put(4, 4);
  CHECK_FALSE(c.contains(2));
  CHECK(*c.mru_key() == 4);
  CHECK(*c.lru_key() == 3);
  c.put(3, 33);
  CHECK(*c.peek(3) == 33);
  CHECK(c.size() == 3);
  c.put(5, 5);
  CHECK_FALSE(c.contains(1));
  CHECK(c.contains(3));
  CHECK(c.contains(4));
  CHECK(c.contains(5));
  CHECK(*c.mru_key() == 5);
  CHECK(*c.lru_key() == 4);
  c.put(6, 6);
  CHECK_FALSE(c.contains(4));
  CHECK(*c.lru_key() == 3);
  CHECK(c.size() == 3);
}

TEST_CASE("nexenne::container::lru_cache string-key eviction sequence is exact") {
  cn::lru_cache<std::string, int, 2> c{};
  c.put("a", 1);
  c.put("b", 2);
  c.put("c", 3);
  CHECK_FALSE(c.contains("a"));
  CHECK(*c.peek("b") == 2);
  c.put("d", 4);
  CHECK_FALSE(c.contains("b"));
  CHECK(c.contains("c"));
  CHECK(c.contains("d"));
  CHECK(*c.mru_key() == "d");
  CHECK(*c.lru_key() == "c");
}

TEST_CASE("nexenne::container::lru_cache mru_key and lru_key after put updates") {
  cache_t<2> c{};
  c.put(1, 10);
  c.put(2, 20);
  CHECK(*c.mru_key() == 2);
  c.put(1, 11);
  CHECK(*c.mru_key() == 1);
  CHECK(*c.lru_key() == 2);
  CHECK(c.erase(1));
  CHECK(*c.mru_key() == 2);
  CHECK(*c.lru_key() == 2);
  CHECK(c.erase(2));
  CHECK(c.mru_key() == nullptr);
  CHECK(c.lru_key() == nullptr);
  CHECK(c.empty());
}

struct mv_key {
  int v{};

  mv_key() noexcept = default;

  explicit mv_key(int const x) noexcept : v{x} {}

  mv_key(mv_key&&) noexcept = default;
  auto operator=(mv_key&&) noexcept -> mv_key& = default;
  mv_key(mv_key const&) = delete;
  auto operator=(mv_key const&) -> mv_key& = delete;

  friend auto operator==(mv_key const& a, mv_key const& b) noexcept -> bool {
    return a.v == b.v;
  }
};

struct mv_key_hash {
  auto operator()(mv_key const& k) const noexcept -> std::size_t {
    return std::hash<int>{}(k.v);
  }
};

TEST_CASE("nexenne::container::lru_cache supports a move-only key") {
  cn::lru_cache<mv_key, int, 2, mv_key_hash> c{};
  c.put(mv_key{1}, 10);
  c.put(mv_key{2}, 20);
  CHECK(c.contains(mv_key{1}));
  REQUIRE(c.get(mv_key{1}) != nullptr);
  CHECK(*c.get(mv_key{1}) == 10);
  c.put(mv_key{3}, 30);
  CHECK_FALSE(c.contains(mv_key{2}));
  CHECK(c.size() == 2);
  CHECK(c.erase(mv_key{3}));
  CHECK_FALSE(c.contains(mv_key{3}));
}

struct resource {
  static inline int alive{0};
  bool owns{false};

  resource() noexcept = default;

  explicit resource(int) noexcept : owns{true} {
    ++alive;
  }

  resource(resource&& other) noexcept : owns{other.owns} {
    other.owns = false;
  }

  auto operator=(resource&& other) noexcept -> resource& {
    if (this != &other) {
      if (owns) {
        --alive;
      }
      owns = other.owns;
      other.owns = false;
    }
    return *this;
  }

  resource(resource const&) = delete;
  auto operator=(resource const&) -> resource& = delete;

  ~resource() noexcept {
    if (owns) {
      --alive;
    }
  }
};

TEST_CASE("nexenne::container::lru_cache erase and clear release the stored value") {
  cn::lru_cache<int, resource, 4> c{};
  CHECK(resource::alive == 0);

  c.put(1, resource{1});
  CHECK(resource::alive == 1);
  CHECK(c.erase(1));
  CHECK(c.size() == 0);
  CHECK(resource::alive == 0);

  c.put(2, resource{1});
  c.put(3, resource{1});
  CHECK(resource::alive == 2);
  c.clear();
  CHECK(c.empty());
  CHECK(resource::alive == 0);
}

TEST_CASE("nexenne::container::lru_cache eviction churn stays exact") {
  cn::lru_cache<int, int, 8> cache;
  for (int i{0}; i < 20000; ++i) {
    cache.put(i, i * 2);
    REQUIRE(cache.contains(i));
    if (i >= 8) {
      CHECK_FALSE(cache.contains(i - 8));
    }
    for (int back{0}; back < 8 && back <= i; ++back) {
      auto const* const value{cache.peek(i - back)};
      REQUIRE(value != nullptr);
      CHECK(*value == (i - back) * 2);
    }
  }
  CHECK(cache.size() == 8);
}

TEST_CASE("nexenne::container::lru_cache peek follows the cache's constness") {
  cn::lru_cache<int, int, 2> c;
  c.put(1, 10);
  c.put(2, 20);
  auto* const value{c.peek(1)};
  REQUIRE(value != nullptr);
  *value = 11;
  REQUIRE(c.lru_key() != nullptr);
  CHECK(*c.lru_key() == 1);
  CHECK(c.peek(3) == nullptr);
  auto const& cc{c};
  static_assert(std::is_same_v<decltype(c.peek(1)), int*>);
  static_assert(std::is_same_v<decltype(cc.peek(1)), int const*>);
  REQUIRE(cc.peek(1) != nullptr);
  CHECK(*cc.peek(1) == 11);
  c.put(3, 30);
  CHECK_FALSE(c.contains(1));
}

}  // namespace
