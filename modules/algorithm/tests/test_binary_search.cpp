/**
 * @file
 * @brief Tests for nexenne::algorithm binary-search variants.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/algorithm/binary_search.hpp>

namespace {

namespace alg = nexenne::algorithm;
using alg::exponential_search;
using alg::find_sorted;
using alg::interpolation_search;

static_assert(find_sorted(std::array{1, 3, 5, 7}, 5) == std::size_t{2});
static_assert(find_sorted(std::array{1, 3, 5, 7}, 4) == std::nullopt);
static_assert(exponential_search(std::array{1, 3, 5, 7}, 1) == std::size_t{0});
static_assert(exponential_search(std::array{1, 3, 5, 7}, 8) == std::nullopt);
static_assert(interpolation_search(std::array{0, 10, 20, 30, 40}, 30) == std::size_t{3});
static_assert(interpolation_search(std::array{0, 10, 20, 30, 40}, 25) == std::nullopt);

template <typename R, typename T>
[[nodiscard]] constexpr auto linear_first(R const& r, T const& value)
  -> std::optional<std::size_t> {
  auto i{std::size_t{0}};
  for (auto const& e : r) {
    if (e == value) {
      return i;
    }
    ++i;
  }
  return std::nullopt;
}

template <typename R, typename T>
void check_all(R const& v, T const& query) {
  auto const expected{linear_first(v, query)};
  CHECK(find_sorted(v, query) == expected);
  CHECK(exponential_search(v, query) == expected);
  if constexpr (std::is_arithmetic_v<std::ranges::range_value_t<R>> && std::is_arithmetic_v<T>) {
    auto const ip{interpolation_search(v, query)};
    CHECK(ip.has_value() == expected.has_value());
    if (ip.has_value()) {
      CHECK(*(std::ranges::begin(v) + static_cast<std::ptrdiff_t>(*ip)) == query);
    }
  }
}

TEST_CASE("nexenne::algorithm::find_sorted on empty and single-element ranges") {
  auto const empty{std::vector<int>{}};
  CHECK(find_sorted(empty, 1) == std::nullopt);

  auto const one{std::array{42}};
  CHECK(find_sorted(one, 42) == std::size_t{0});
  CHECK(find_sorted(one, 41) == std::nullopt);
  CHECK(find_sorted(one, 43) == std::nullopt);
}

TEST_CASE("nexenne::algorithm::find_sorted hits front, middle, back and misses around them") {
  auto const v{std::array{2, 4, 6, 8, 10}};
  CHECK(find_sorted(v, 2) == std::size_t{0});
  CHECK(find_sorted(v, 6) == std::size_t{2});
  CHECK(find_sorted(v, 10) == std::size_t{4});
  CHECK(find_sorted(v, 1) == std::nullopt);
  CHECK(find_sorted(v, 5) == std::nullopt);
  CHECK(find_sorted(v, 11) == std::nullopt);
}

TEST_CASE("nexenne::algorithm::find_sorted returns the FIRST of equal elements") {
  auto const v{std::array{1, 2, 2, 2, 3}};
  CHECK(find_sorted(v, 2) == std::size_t{1});
  CHECK(find_sorted(v, 1) == std::size_t{0});
  CHECK(find_sorted(v, 3) == std::size_t{4});
}

TEST_CASE("nexenne::algorithm::find_sorted on a non-arithmetic ordered type") {
  auto const v{std::array<std::string, 4>{"alpha", "bravo", "charlie", "delta"}};
  CHECK(find_sorted(v, std::string{"charlie"}) == std::size_t{2});
  CHECK(find_sorted(v, std::string{"echo"}) == std::nullopt);
  CHECK(find_sorted(v, std::string_view{"alpha"}) == std::size_t{0});
}

TEST_CASE("nexenne::algorithm::exponential_search matches find_sorted everywhere") {
  auto const v{std::array{1, 3, 5, 7, 9, 11, 13, 15, 17, 19}};
  for (auto query{-1}; query <= 21; ++query) {
    CAPTURE(query);
    CHECK(exponential_search(v, query) == find_sorted(v, query));
  }
}

TEST_CASE("nexenne::algorithm::exponential_search is strong near the front of a large range") {
  auto big{std::vector<int>{}};
  big.reserve(100000);
  for (auto i{0}; i < 100000; ++i) {
    big.push_back(i * 2);
  }
  CHECK(exponential_search(big, 0) == std::size_t{0});
  CHECK(exponential_search(big, 6) == std::size_t{3});
  CHECK(exponential_search(big, 199998) == std::size_t{99999});
  CHECK(exponential_search(big, 7) == std::nullopt);
  CHECK(exponential_search(big, -2) == std::nullopt);
  CHECK(exponential_search(big, 200000) == std::nullopt);
}

TEST_CASE("nexenne::algorithm::exponential_search empty and single") {
  auto const empty{std::vector<int>{}};
  CHECK(exponential_search(empty, 1) == std::nullopt);
  auto const one{std::array{7}};
  CHECK(exponential_search(one, 7) == std::size_t{0});
  CHECK(exponential_search(one, 8) == std::nullopt);
}

TEST_CASE("nexenne::algorithm::interpolation_search on uniformly distributed data") {
  auto const v{std::array{0, 10, 20, 30, 40, 50, 60, 70, 80, 90}};
  for (auto i{std::size_t{0}}; i < v.size(); ++i) {
    CAPTURE(i);
    CHECK(interpolation_search(v, v[i]) == i);
  }
  CHECK(interpolation_search(v, 5) == std::nullopt);
  CHECK(interpolation_search(v, -1) == std::nullopt);
  CHECK(interpolation_search(v, 91) == std::nullopt);
}

TEST_CASE("nexenne::algorithm::interpolation_search edge cases") {
  CHECK(interpolation_search(std::vector<int>{}, 1) == std::nullopt);
  CHECK(interpolation_search(std::array{5}, 5) == std::size_t{0});
  CHECK(interpolation_search(std::array{5}, 6) == std::nullopt);
  CHECK(interpolation_search(std::array{2, 2, 2, 2}, 2) != std::nullopt);
  CHECK(interpolation_search(std::array{2, 2, 2, 2}, 3) == std::nullopt);
  CHECK(interpolation_search(std::array{-50, -20, 0, 25, 80}, -20) == std::size_t{1});
}

TEST_CASE("nexenne::algorithm::interpolation_search on floating point") {
  auto const v{std::array{0.0, 1.5, 3.0, 4.5, 6.0}};
  CHECK(interpolation_search(v, 3.0) == std::size_t{2});
  CHECK(interpolation_search(v, 4.5) == std::size_t{3});
  CHECK(interpolation_search(v, 2.0) == std::nullopt);
}

TEST_CASE("nexenne::algorithm: all three searches agree with a linear reference (differential)") {
  auto rng{std::uint32_t{0x1234567u}};
  auto next{[&rng] {
    rng = rng * 1103515245u + 12345u;
    return static_cast<int>((rng >> 16) % 40);
  }};
  for (auto trial{0}; trial < 200; ++trial) {
    auto v{std::vector<int>{}};
    auto const len{static_cast<std::size_t>(next() % 25)};
    for (std::size_t i{0}; i < len; ++i) {
      v.push_back(next());
    }
    std::ranges::sort(v);
    for (auto query{-2}; query <= 41; ++query) {
      auto const expected{linear_first(v, query)};
      CHECK(find_sorted(v, query) == expected);
      CHECK(exponential_search(v, query) == expected);
      auto const ip{interpolation_search(v, query)};
      CHECK(ip.has_value() == expected.has_value());
      if (ip.has_value()) {
        CHECK(v[*ip] == query);
      }
    }
  }
}

TEST_CASE("nexenne::algorithm: all-equal ranges") {
  auto const v{std::array{7, 7, 7, 7, 7}};
  CHECK(find_sorted(v, 7) == std::size_t{0});
  CHECK(exponential_search(v, 7) == std::size_t{0});
  CHECK(interpolation_search(v, 7).has_value());
  CHECK(*interpolation_search(v, 7) < v.size());
  CHECK(find_sorted(v, 6) == std::nullopt);
  CHECK(exponential_search(v, 8) == std::nullopt);
  CHECK(interpolation_search(v, 8) == std::nullopt);
}

TEST_CASE("nexenne::algorithm: two-element ranges, every query") {
  auto const v{std::array{3, 9}};
  CHECK(find_sorted(v, 3) == std::size_t{0});
  CHECK(find_sorted(v, 9) == std::size_t{1});
  CHECK(find_sorted(v, 1) == std::nullopt);
  CHECK(find_sorted(v, 5) == std::nullopt);
  CHECK(find_sorted(v, 12) == std::nullopt);
  for (auto q{0}; q <= 12; ++q) {
    CAPTURE(q);
    CHECK(exponential_search(v, q) == find_sorted(v, q));
    auto const ip{interpolation_search(v, q)};
    CHECK(ip.has_value() == find_sorted(v, q).has_value());
  }
}

TEST_CASE("nexenne::algorithm: unsigned element type") {
  auto const v{std::array<unsigned, 5>{1u, 4u, 9u, 16u, 25u}};
  CHECK(find_sorted(v, 9u) == std::size_t{2});
  CHECK(find_sorted(v, 10u) == std::nullopt);
  CHECK(exponential_search(v, 1u) == std::size_t{0});
  CHECK(interpolation_search(v, 25u) == std::size_t{4});
  CHECK(interpolation_search(v, 0u) == std::nullopt);
}

TEST_CASE("nexenne::algorithm::interpolation_search survives an extreme-magnitude span") {
  constexpr auto lo{std::numeric_limits<int>::min()};
  constexpr auto hi{std::numeric_limits<int>::max()};
  auto const v{std::array{lo, -1, 0, 1, hi}};
  CHECK(interpolation_search(v, hi) == std::size_t{4});
  CHECK(interpolation_search(v, lo) == std::size_t{0});
  CHECK(interpolation_search(v, 0) == std::size_t{2});
  CHECK(interpolation_search(v, 12345) == std::nullopt);
  CHECK(find_sorted(v, hi) == std::size_t{4});
  CHECK(exponential_search(v, lo) == std::size_t{0});
}

TEST_CASE("nexenne::algorithm::interpolation_search on clustered (non-uniform) data") {
  auto const v{std::array{1, 2, 3, 4, 1000, 1001, 1002, 1003}};
  for (auto const x : v) {
    CAPTURE(x);
    auto const r{interpolation_search(v, x)};
    REQUIRE(r.has_value());
    CHECK(v[*r] == x);
  }
  CHECK(interpolation_search(v, 500) == std::nullopt);
  CHECK(interpolation_search(v, 1004) == std::nullopt);
}

TEST_CASE("nexenne::algorithm: exhaustive size x query sweep (every boundary)") {
  for (auto len{std::size_t{0}}; len <= 40; ++len) {
    auto v{std::vector<int>{}};
    v.reserve(len);
    for (auto i{std::size_t{0}}; i < len; ++i) {
      v.push_back(static_cast<int>(i) * 2);
    }
    for (auto q{-2}; q <= static_cast<int>(len) * 2 + 2; ++q) {
      check_all(v, q);
    }
  }
}

TEST_CASE("nexenne::algorithm: exhaustive duplicates sweep") {
  for (auto run{1}; run <= 4; ++run) {
    for (auto distinct{std::size_t{1}}; distinct <= 12; ++distinct) {
      auto v{std::vector<int>{}};
      for (auto value{std::size_t{0}}; value < distinct; ++value) {
        for (auto r{0}; r < run; ++r) {
          v.push_back(static_cast<int>(value));
        }
      }
      for (auto q{-1}; q <= static_cast<int>(distinct) + 1; ++q) {
        check_all(v, q);
      }
    }
  }
}

template <typename T>
void sweep_element_type() {
  for (auto const len : {0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33}) {
    auto v{std::vector<T>{}};
    for (auto i{0}; i < len; ++i) {
      v.push_back(static_cast<T>(i * 2));
    }
    for (auto q{0}; q <= len * 2 + 1; ++q) {
      check_all(v, static_cast<T>(q));
    }
  }
}

TEST_CASE("nexenne::algorithm: element-type matrix (signed/unsigned widths, char, float, double)") {
  sweep_element_type<std::int8_t>();
  sweep_element_type<std::int16_t>();
  sweep_element_type<std::int32_t>();
  sweep_element_type<std::int64_t>();
  sweep_element_type<std::uint8_t>();
  sweep_element_type<std::uint16_t>();
  sweep_element_type<std::uint32_t>();
  sweep_element_type<std::uint64_t>();
  sweep_element_type<char>();
  sweep_element_type<float>();
  sweep_element_type<double>();
}

TEST_CASE("nexenne::algorithm: range-type matrix (array, vector, deque, span)") {
  auto const vec{std::vector<int>{1, 3, 5, 7, 9, 11}};
  auto const arr{std::array{1, 3, 5, 7, 9, 11}};
  auto const deq{std::deque<int>{1, 3, 5, 7, 9, 11}};
  auto const sp{std::span<int const>{vec}};
  for (auto q{0}; q <= 12; ++q) {
    check_all(vec, q);
    check_all(arr, q);
    check_all(deq, q);
    check_all(sp, q);
  }
}

TEST_CASE("nexenne::algorithm: wide-magnitude differential (stresses interpolation arithmetic)") {
  auto rng{std::uint64_t{0xD1CED00Du}};
  auto next{[&rng] {
    rng = rng * 6364136223846793005ull + 1442695040888963407ull;
    return rng >> 11;
  }};
  auto draw{[&next] { return static_cast<long long>(next() % 2000000001ull) - 1000000000ll; }};

  for (auto trial{0}; trial < 300; ++trial) {
    auto v{std::vector<long long>{}};
    auto const len{next() % 80};
    for (auto i{std::uint64_t{0}}; i < len; ++i) {
      v.push_back(draw());
    }
    std::ranges::sort(v);
    for (auto s{0}; s < 25; ++s) {
      check_all(v, draw());
      if (!v.empty()) {
        check_all(v, v[next() % v.size()]);
      }
    }
    if (!v.empty()) {
      check_all(v, v.front());
      check_all(v, v.back());
    }
  }
}

namespace probe {

struct nothrow_ord {
  int v;

  auto operator<=>(nothrow_ord const& o) const noexcept -> std::strong_ordering {
    return v <=> o.v;
  }

  auto operator==(nothrow_ord const& o) const noexcept -> bool {
    return v == o.v;
  }
};

struct maythrow_ord {
  int v;

  auto operator<=>(maythrow_ord const& o) const -> std::strong_ordering {
    return v <=> o.v;
  }

  auto operator==(maythrow_ord const& o) const -> bool {
    return v == o.v;
  }
};

}  // namespace probe

TEST_CASE("nexenne::algorithm searches are conditionally noexcept over the comparison") {
  static_assert(noexcept(find_sorted(
    std::declval<std::array<probe::nothrow_ord, 3>&>(), std::declval<probe::nothrow_ord const&>()
  )));
  static_assert(!noexcept(find_sorted(
    std::declval<std::array<probe::maythrow_ord, 3>&>(), std::declval<probe::maythrow_ord const&>()
  )));
  static_assert(noexcept(exponential_search(
    std::declval<std::array<probe::nothrow_ord, 3>&>(), std::declval<probe::nothrow_ord const&>()
  )));
  static_assert(!noexcept(exponential_search(
    std::declval<std::array<probe::maythrow_ord, 3>&>(), std::declval<probe::maythrow_ord const&>()
  )));
  CHECK(true);
}

TEST_CASE("nexenne::algorithm::interpolation_search handles an infinite end") {
  auto const inf{std::numeric_limits<double>::infinity()};
  auto const values{std::array<double, 3>{-inf, 1.0, 2.0}};
  CHECK(alg::interpolation_search(values, 1.0) == alg::found_index{1});
  CHECK(alg::interpolation_search(values, 2.0) == alg::found_index{2});
}

template <typename Range, typename Value>
concept interpolation_searchable =
  requires(Range const& r, Value const v) { alg::interpolation_search(r, v); };

static_assert(!interpolation_searchable<std::array<std::uint64_t, 2>, int>);
static_assert(interpolation_searchable<std::array<std::uint64_t, 2>, unsigned>);

}  // namespace
