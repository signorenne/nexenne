/**
 * @file
 * @brief Tests for the container module's noexcept policy.
 *
 * A member that runs element or caller code (an element's copy, move or
 * construction, a comparator, a hasher, a key equality) is noexcept exactly
 * when that code is. Each container is checked with an element whose copy can
 * throw (its move cannot) and with a nothrow element, and the comparator and
 * hasher paths with a throwing callable.
 */

#include <doctest/doctest.h>

#include <compare>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nexenne/container/bag.hpp>
#include <nexenne/container/bimap.hpp>
#include <nexenne/container/binary_tree.hpp>
#include <nexenne/container/bloom_filter.hpp>
#include <nexenne/container/dense_map.hpp>
#include <nexenne/container/deque.hpp>
#include <nexenne/container/flat_hash_map.hpp>
#include <nexenne/container/flat_hash_set.hpp>
#include <nexenne/container/flat_map.hpp>
#include <nexenne/container/flat_set.hpp>
#include <nexenne/container/gap_buffer.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/container/heap.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>
#include <nexenne/container/linear_arena.hpp>
#include <nexenne/container/lru_cache.hpp>
#include <nexenne/container/mpmc_queue.hpp>
#include <nexenne/container/mpsc_queue.hpp>
#include <nexenne/container/object_pool.hpp>
#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/container/slot_map.hpp>
#include <nexenne/container/small_vector.hpp>
#include <nexenne/container/spsc_queue.hpp>
#include <nexenne/container/stable_vector.hpp>
#include <nexenne/container/static_flat_map.hpp>
#include <nexenne/container/static_vector.hpp>
#include <nexenne/container/trie.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace cn = nexenne::container;

struct throwing_copy {
  int value{0};

  throwing_copy() noexcept = default;

  explicit throwing_copy(int const v) noexcept : value{v} {}

  throwing_copy(throwing_copy const& other) noexcept(false) : value{other.value} {}

  throwing_copy(throwing_copy&& other) noexcept = default;

  ~throwing_copy() = default;

  auto operator=(throwing_copy const& other) noexcept(false) -> throwing_copy& = default;

  auto operator=(throwing_copy&& other) noexcept -> throwing_copy& = default;

  friend auto operator==(throwing_copy const& a, throwing_copy const& b) noexcept -> bool = default;
  friend auto operator<=>(throwing_copy const& a, throwing_copy const& b) noexcept = default;
};

struct throwing_copy_hash {
  [[nodiscard]] auto operator()(throwing_copy const& t) const noexcept -> std::size_t {
    return std::hash<int>{}(t.value);
  }
};

struct nothrow_less {
  template <typename T>
  [[nodiscard]] auto operator()(T const& a, T const& b) const noexcept -> bool {
    return a < b;
  }
};

struct nothrow_equal {
  template <typename T>
  [[nodiscard]] auto operator()(T const& a, T const& b) const noexcept -> bool {
    return a == b;
  }
};

struct nothrow_hash {
  [[nodiscard]] auto operator()(int const k) const noexcept -> std::size_t {
    return static_cast<std::size_t>(k);
  }
};

struct throwing_less {
  [[nodiscard]] auto operator()(int const a, int const b) const noexcept(false) -> bool {
    return a < b;
  }
};

struct throwing_hash {
  [[nodiscard]] auto operator()(int const k) const noexcept(false) -> std::size_t {
    return std::hash<int>{}(k);
  }
};

using tc = throwing_copy;

static_assert(
  !noexcept(std::declval<cn::static_vector<tc, 4>&>().push_back(std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::static_vector<tc, 4>&>().push_back(tc{})));
static_assert(noexcept(std::declval<cn::static_vector<tc, 4>&>().emplace_back(1)));
static_assert(!std::is_nothrow_copy_constructible_v<cn::static_vector<tc, 4>>);
static_assert(std::is_nothrow_move_constructible_v<cn::static_vector<tc, 4>>);
static_assert(
  noexcept(std::declval<cn::static_vector<int, 4>&>().push_back(std::declval<int const&>()))
);
static_assert(std::is_nothrow_copy_constructible_v<cn::static_vector<int, 4>>);

static_assert(
  !noexcept(std::declval<cn::small_vector<tc, 2>&>().push_back(std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::small_vector<tc, 2>&>().push_back(tc{})));
static_assert(!std::is_nothrow_copy_constructible_v<cn::small_vector<tc, 2>>);
static_assert(noexcept(std::declval<cn::small_vector<int, 2>&>().push_back(1)));
static_assert(std::is_nothrow_copy_constructible_v<cn::small_vector<int, 2>>);

static_assert(!noexcept(std::declval<cn::deque<tc>&>().push_back(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::deque<tc>&>().push_front(tc{})));
static_assert(noexcept(std::declval<cn::deque<tc>&>().pop_back()));
static_assert(noexcept(std::declval<cn::deque<int>&>().push_back(std::declval<int const&>())));

static_assert(
  !noexcept(std::declval<cn::stable_vector<tc>&>().push_back(std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::stable_vector<tc>&>().emplace_back(3)));
static_assert(
  noexcept(std::declval<cn::stable_vector<int>&>().push_back(std::declval<int const&>()))
);

static_assert(!noexcept(std::declval<cn::ring_buffer<tc, 4>&>().push(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::ring_buffer<tc, 4>&>().push_overwrite(tc{})));
static_assert(noexcept(std::declval<cn::ring_buffer<tc, 4>&>().pop()));
static_assert(noexcept(std::declval<cn::ring_buffer<int, 4>&>().push(std::declval<int const&>())));

static_assert(!noexcept(std::declval<cn::gap_buffer<tc>&>().insert(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::gap_buffer<tc>&>().insert(tc{})));
static_assert(noexcept(std::declval<cn::gap_buffer<int>&>().insert(std::declval<int const&>())));

static_assert(!noexcept(std::declval<cn::bag<tc>&>().insert(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::bag<tc>&>().insert(tc{})));
static_assert(noexcept(std::declval<cn::bag<int>&>().insert(std::declval<int const&>())));

static_assert(!noexcept(std::declval<cn::slot_map<tc>&>().insert(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::slot_map<tc>&>().insert(tc{})));
static_assert(noexcept(std::declval<cn::slot_map<int>&>().insert(std::declval<int const&>())));

static_assert(!noexcept(std::declval<cn::spsc_queue<tc, 4>&>().push(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::spsc_queue<tc, 4>&>().push(tc{})));
static_assert(noexcept(std::declval<cn::spsc_queue<tc, 4>&>().pop()));
static_assert(!noexcept(std::declval<cn::mpsc_queue<tc, 4>&>().push(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::mpsc_queue<int, 4>&>().push(std::declval<int const&>())));
static_assert(!noexcept(std::declval<cn::mpmc_queue<tc, 4>&>().push(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::mpmc_queue<int, 4>&>().push(std::declval<int const&>())));

static_assert(
  !noexcept(std::declval<cn::flat_set<tc, nothrow_less>&>().insert(std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::flat_set<tc, nothrow_less>&>().insert(tc{})));
static_assert(noexcept(std::declval<cn::flat_set<int, nothrow_less>&>().contains(1)));
static_assert(!noexcept(std::declval<cn::flat_set<int, throwing_less>&>().contains(1)));

static_assert(!noexcept(std::declval<cn::flat_map<int, tc, nothrow_less>&>()
                          .insert_or_assign(1, std::declval<tc const&>())));
static_assert(
  noexcept(std::declval<cn::flat_map<int, tc, nothrow_less>&>().insert_or_assign(1, tc{}))
);
static_assert(noexcept(std::declval<cn::flat_map<int, int, nothrow_less>&>().find(1)));
static_assert(!noexcept(std::declval<cn::flat_map<int, int, throwing_less>&>().find(1)));

static_assert(!noexcept(std::declval<cn::static_flat_map<int, tc, 4, nothrow_less>&>()
                          .insert_or_assign(1, std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::static_flat_map<int, int, 4, nothrow_less>&>().find(1)));
static_assert(!noexcept(std::declval<cn::static_flat_map<int, int, 4, throwing_less>&>().find(1)));

static_assert(
  !noexcept(std::declval<cn::binary_tree<tc, nothrow_less>&>().insert(std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::binary_tree<tc, nothrow_less>&>().insert(tc{})));
static_assert(noexcept(std::declval<cn::binary_tree<int, nothrow_less>&>().contains(1)));
static_assert(!noexcept(std::declval<cn::binary_tree<int, throwing_less>&>().contains(1)));

static_assert(
  !noexcept(std::declval<cn::heap<tc, nothrow_less>&>().push(std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::heap<tc, nothrow_less>&>().push(tc{})));
static_assert(noexcept(std::declval<cn::heap<int, nothrow_less>&>().pop()));
static_assert(!noexcept(std::declval<cn::heap<int, throwing_less>&>().pop()));

static_assert(!noexcept(std::declval<cn::indexed_priority_queue<tc, nothrow_less>&>()
                          .push(std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::indexed_priority_queue<tc, nothrow_less>&>().push(tc{})));
static_assert(!noexcept(std::declval<cn::indexed_priority_queue<int, throwing_less>&>().pop()));

static_assert(!noexcept(std::declval<cn::flat_hash_map<int, tc, nothrow_hash, nothrow_equal>&>()
                          .insert(1, std::declval<tc const&>())));
static_assert(
  noexcept(std::declval<cn::flat_hash_map<int, tc, nothrow_hash, nothrow_equal>&>().insert(1, tc{}))
);
static_assert(
  noexcept(std::declval<cn::flat_hash_map<int, int, nothrow_hash, nothrow_equal>&>().find(1))
);
static_assert(!noexcept(std::declval<cn::flat_hash_map<int, int, throwing_hash>&>().find(1)));
static_assert(!noexcept(std::declval<cn::flat_hash_map<int, int, throwing_hash>&>().erase(1)));

static_assert(!noexcept(std::declval<cn::flat_hash_set<tc, throwing_copy_hash, nothrow_equal>&>()
                          .insert(std::declval<tc const&>())));
static_assert(
  noexcept(std::declval<cn::flat_hash_set<int, nothrow_hash, nothrow_equal>&>().contains(1))
);
static_assert(!noexcept(std::declval<cn::flat_hash_set<int, throwing_hash>&>().contains(1)));

static_assert(
  noexcept(std::declval<cn::bimap<int, int, nothrow_hash, nothrow_hash>&>().insert(1, 2))
);
static_assert(noexcept(std::declval<cn::bimap<int, int>&>().find_by_left(1)));
static_assert(!noexcept(std::declval<cn::bimap<int, int, throwing_hash>&>().insert(1, 2)));

static_assert(
  noexcept(std::declval<cn::lru_cache<int, int, 4, nothrow_hash, nothrow_equal>&>().get(1))
);
static_assert(!noexcept(std::declval<cn::lru_cache<int, int, 4, throwing_hash>&>().get(1)));

static_assert(noexcept(std::declval<cn::bloom_filter<int, nothrow_hash>&>().contains(1)));
static_assert(!noexcept(std::declval<cn::bloom_filter<int, throwing_hash>&>().contains(1)));

static_assert(!noexcept(std::declval<cn::dense_map<unsigned, tc>&>()
                          .insert_or_assign(1U, std::declval<tc const&>())));
static_assert(noexcept(std::declval<cn::dense_map<unsigned, tc>&>().insert_or_assign(1U, tc{})));

static_assert(
  !noexcept(std::declval<cn::trie<char, tc>&>().insert("a", std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::trie<char, tc>&>().insert("a", tc{})));
static_assert(noexcept(std::declval<cn::trie<char, int>&>().contains("a")));

static_assert(
  !noexcept(std::declval<cn::object_pool<tc, 4>&>().emplace(std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::object_pool<tc, 4>&>().emplace(1)));
static_assert(
  !noexcept(std::declval<cn::linear_arena<64>&>().emplace<tc>(std::declval<tc const&>()))
);
static_assert(noexcept(std::declval<cn::linear_arena<64>&>().emplace<tc>(1)));
static_assert(noexcept(std::declval<cn::graph<int>&>().add_edge(0, 1, 2)));
static_assert(noexcept(std::declval<cn::graph<tc>&>().remove_edge(0, 1)));

static_assert(noexcept(std::declval<cn::flat_set<int>&>().contains(1)));
static_assert(noexcept(std::declval<cn::flat_map<int, int>&>().find(1)));
static_assert(noexcept(std::declval<cn::static_flat_map<int, int, 4>&>().find(1)));
static_assert(noexcept(std::declval<cn::binary_tree<int>&>().contains(1)));
static_assert(noexcept(std::declval<cn::heap<int>&>().pop()));
static_assert(noexcept(std::declval<cn::heap<int, std::greater<int>>&>().pop()));
static_assert(noexcept(std::declval<cn::indexed_priority_queue<int>&>().pop()));
static_assert(noexcept(std::declval<cn::flat_hash_map<int, int>&>().find(1)));
static_assert(noexcept(std::declval<cn::flat_hash_set<int>&>().contains(1)));
static_assert(noexcept(std::declval<cn::flat_hash_set<std::string_view>&>().contains("a")));
static_assert(noexcept(std::declval<cn::lru_cache<int, int, 4>&>().get(1)));
static_assert(noexcept(std::declval<cn::bloom_filter<int>&>().contains(1)));
static_assert(noexcept(std::declval<cn::flat_set<int, std::less<>>&>().contains(1)));
static_assert(noexcept(std::declval<cn::flat_map<int, int, std::greater<>>&>().find(1)));

struct throwing_ops {
  int value{0};

  friend auto operator<(throwing_ops const& a, throwing_ops const& b) noexcept(false) -> bool {
    if (a.value < 0 || b.value < 0) {
      throw std::runtime_error{"negative key"};
    }
    return a.value < b.value;
  }

  friend auto operator==(throwing_ops const& a, throwing_ops const& b) noexcept(false) -> bool {
    if (a.value < 0 || b.value < 0) {
      throw std::runtime_error{"negative key"};
    }
    return a.value == b.value;
  }
};

struct throwing_ops_hash {
  [[nodiscard]] auto operator()(throwing_ops const& k) const noexcept -> std::size_t {
    return std::hash<int>{}(k.value);
  }
};

struct plain_less {
  [[nodiscard]] auto operator()(int const a, int const b) const -> bool {
    return a < b;
  }
};

static_assert(!noexcept(std::declval<cn::flat_set<throwing_ops>&>().contains(throwing_ops{})));
static_assert(
  !noexcept(std::declval<cn::flat_set<throwing_ops, std::less<>>&>().contains(throwing_ops{}))
);
static_assert(!noexcept(std::declval<cn::flat_hash_set<throwing_ops, throwing_ops_hash>&>()
                          .contains(throwing_ops{})));
static_assert(!noexcept(std::declval<cn::flat_set<int, plain_less>&>().contains(1)));

static_assert(cn::detail::nothrow_invocable_v<std::less<int> const&, int const&, int const&>);
static_assert(cn::detail::nothrow_invocable_v<std::less<>&, int const&, long const&>);
static_assert(cn::detail::nothrow_invocable_v<std::greater<int> const&, int const&, int const&>);
static_assert(cn::detail::nothrow_invocable_v<std::equal_to<> const&, int const&, int const&>);
static_assert(cn::detail::nothrow_invocable_v<
              std::equal_to<std::string_view> const&,
              std::string_view const&,
              std::string_view const&>);
static_assert(cn::detail::nothrow_invocable_v<std::hash<int> const&, int const&>);
static_assert(cn::detail::nothrow_invocable_v<std::hash<int const*> const&, int const* const&>);
static_assert(
  cn::detail::nothrow_invocable_v<std::hash<std::string_view> const&, std::string_view const&>
);
static_assert(
  !cn::detail::
    nothrow_invocable_v<std::less<throwing_ops> const&, throwing_ops const&, throwing_ops const&>
);
static_assert(
  !cn::detail::nothrow_invocable_v<std::less<> const&, throwing_ops const&, throwing_ops const&>
);
static_assert(!cn::detail::nothrow_invocable_v<
              std::equal_to<throwing_ops> const&,
              throwing_ops const&,
              throwing_ops const&>);
static_assert(
  !cn::detail::
    nothrow_invocable_v<std::less<std::string> const&, char const* const&, char const* const&>
);
static_assert(!cn::detail::nothrow_invocable_v<plain_less const&, int const&, int const&>);
static_assert(!cn::detail::nothrow_invocable_v<throwing_less const&, int const&, int const&>);
static_assert(cn::detail::nothrow_invocable_v<nothrow_less const&, int const&, int const&>);
static_assert(
  !cn::detail::nothrow_invocable_v<std::less<int> const&, std::string const&, int const&>
);

static_assert(std::is_nothrow_destructible_v<cn::static_vector<tc, 4>>);
static_assert(std::is_nothrow_destructible_v<cn::spsc_queue<tc, 4>>);

TEST_CASE("nexenne::container noexcept follows the element and caller code (container-28)") {
  // A copy that throws now propagates to the caller instead of terminating.
  struct copy_fails {
    copy_fails() noexcept = default;

    copy_fails(copy_fails const&) noexcept(false) {
      throw std::runtime_error{"copy failed"};
    }

    copy_fails(copy_fails&&) noexcept = default;
    ~copy_fails() = default;
    auto operator=(copy_fails const&) noexcept(false) -> copy_fails& = default;
    auto operator=(copy_fails&&) noexcept -> copy_fails& = default;
  };

  cn::static_vector<copy_fails, 2> v;
  copy_fails const source{};
  CHECK_THROWS_AS(nexenne::utility::ignore(v.push_back(source)), std::runtime_error);
  CHECK(v.empty());
  CHECK(v.push_back(copy_fails{}).has_value());
  CHECK(v.size() == 1);
  CHECK(tc{1} == tc{1});
  auto const order{tc{1} <=> tc{2}};
  CHECK(std::is_lt(order));
}

TEST_CASE("nexenne::container a throw through a standard comparator propagates (container-28)") {
  // std::less and std::equal_to over a key whose operators may throw are not
  // trusted, so the lookup is not noexcept and the throw reaches the caller.
  cn::flat_set<throwing_ops> ordered;
  REQUIRE(ordered.insert(throwing_ops{1}).second);
  CHECK(ordered.contains(throwing_ops{1}));
  CHECK_THROWS_AS(
    nexenne::utility::ignore(ordered.contains(throwing_ops{-1})), std::runtime_error
  );

  cn::flat_hash_set<throwing_ops, throwing_ops_hash> hashed;
  REQUIRE(hashed.insert(throwing_ops{-1}));
  CHECK_THROWS_AS(nexenne::utility::ignore(hashed.contains(throwing_ops{-1})), std::runtime_error);
}

}  // namespace
