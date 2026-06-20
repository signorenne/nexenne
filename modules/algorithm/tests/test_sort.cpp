/**
 * @file
 * @brief Tests for nexenne::algorithm integer sorts (counting_sort, radix_sort).
 *
 * Both sorts are validated differentially against std::sort over random inputs
 * of every unsigned width and many shapes (random, already-sorted, reversed,
 * all-equal, all-zero, all-max), across the lengths that exercise bucket and
 * digit-pass boundaries. radix_sort is also checked through its caller-scratch
 * overload and on the odd-sizeof copy-back path (uint8).
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <nexenne/algorithm/sort/counting_sort.hpp>
#include <nexenne/algorithm/sort/radix_sort.hpp>

namespace {

namespace alg = nexenne::algorithm;

struct lcg {
  std::uint64_t state{0x243F6A8885A308D3ull};

  auto next() -> std::uint64_t {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state >> 11;
  }
};

template <std::unsigned_integral T>
[[nodiscard]] auto random_values(lcg& gen, std::size_t const n) -> std::vector<T> {
  auto v{std::vector<T>(n)};
  for (auto& x : v) {
    x = static_cast<T>(gen.next());
  }
  return v;
}

template <std::unsigned_integral T>
[[nodiscard]] auto
bounded_values(lcg& gen, std::size_t const n, T const max_value) -> std::vector<T> {
  auto v{std::vector<T>(n)};
  for (auto& x : v) {
    x = static_cast<T>(gen.next() % (static_cast<std::uint64_t>(max_value) + 1));
  }
  return v;
}

constexpr auto lengths{std::array<std::size_t, 11>{0, 1, 2, 3, 5, 16, 64, 255, 256, 257, 1000}};

// counting_sort

TEST_CASE("nexenne::algorithm::counting_sort known small case and edges") {
  auto v{std::vector<std::uint8_t>{3, 1, 2, 1, 0}};
  alg::counting_sort(std::span<std::uint8_t>{v}, std::uint8_t{3});
  CHECK(v == std::vector<std::uint8_t>{0, 1, 1, 2, 3});

  auto empty{std::vector<std::uint32_t>{}};
  alg::counting_sort(std::span<std::uint32_t>{empty}, 10u);  // no-op, no crash
  CHECK(empty.empty());

  auto one{std::vector<std::uint32_t>{42}};
  alg::counting_sort(std::span<std::uint32_t>{one}, 100u);
  CHECK(one == std::vector<std::uint32_t>{42});
}

template <std::unsigned_integral T>
void counting_matches_std(lcg& gen, T const max_value) {
  for (auto const len : lengths) {
    CAPTURE(len);
    auto data{bounded_values<T>(gen, len, max_value)};
    auto ref{data};
    std::ranges::sort(ref);
    alg::counting_sort(std::span<T>{data}, max_value);
    CHECK(data == ref);
  }
}

TEST_CASE("nexenne::algorithm::counting_sort matches std::sort across widths and ranges") {
  auto gen{lcg{}};
  SUBCASE("uint8, full range K=256") {
    counting_matches_std<std::uint8_t>(gen, 255);
  }
  SUBCASE("uint16, K larger than N") {
    counting_matches_std<std::uint16_t>(gen, 4000);
  }
  SUBCASE("uint16, K smaller than N") {
    counting_matches_std<std::uint16_t>(gen, 7);
  }
  SUBCASE("uint32, tiny range") {
    counting_matches_std<std::uint32_t>(gen, 3u);
  }
  SUBCASE("uint64, tiny range") {
    counting_matches_std<std::uint64_t>(gen, 5u);
  }
}

TEST_CASE("nexenne::algorithm::counting_sort handles degenerate distributions") {
  auto run{[](std::vector<std::uint16_t> data, std::uint16_t max_value) {
    auto ref{data};
    std::ranges::sort(ref);
    alg::counting_sort(std::span<std::uint16_t>{data}, max_value);
    CHECK(data == ref);
  }};
  run(std::vector<std::uint16_t>(50, std::uint16_t{7}), 7);  // all equal
  run(std::vector<std::uint16_t>(50, std::uint16_t{0}), 0);  // all zero, K=1
  run({0, 1, 2, 3, 4, 5, 6, 7}, 7);                          // already sorted
  run({7, 6, 5, 4, 3, 2, 1, 0}, 7);                          // reverse sorted
  run({100, 0, 100, 0, 100}, 100);                           // two-value
}

TEST_CASE("nexenne::algorithm::counting_sort iterator overload matches span overload") {
  auto gen{lcg{}};
  auto data{bounded_values<std::uint16_t>(gen, 300, 200)};
  auto by_span{data};
  alg::counting_sort(std::span<std::uint16_t>{by_span}, std::uint16_t{200});
  alg::counting_sort(data.begin(), data.end(), std::uint16_t{200});
  CHECK(data == by_span);
}

// radix_sort

TEST_CASE("nexenne::algorithm::radix_sort known small case and edges") {
  auto v{std::vector<std::uint32_t>{5, 3, 8, 1, 9, 2}};
  alg::radix_sort(std::span<std::uint32_t>{v});
  CHECK(v == std::vector<std::uint32_t>{1, 2, 3, 5, 8, 9});

  auto empty{std::vector<std::uint32_t>{}};
  alg::radix_sort(std::span<std::uint32_t>{empty});
  CHECK(empty.empty());

  auto one{std::vector<std::uint64_t>{99}};
  alg::radix_sort(std::span<std::uint64_t>{one});
  CHECK(one == std::vector<std::uint64_t>{99});
}

template <std::unsigned_integral T>
void radix_matches_std(lcg& gen) {
  for (auto const len : lengths) {
    CAPTURE(len);
    auto const data{random_values<T>(gen, len)};
    auto ref{data};
    std::ranges::sort(ref);

    auto alloc{data};
    alg::radix_sort(std::span<T>{alloc});
    CHECK(alloc == ref);

    // Caller-supplied scratch produces the same result with no allocation.
    auto scratched{data};
    auto scratch{std::vector<T>(len)};
    alg::radix_sort(std::span<T>{scratched}, std::span<T>{scratch});
    CHECK(scratched == ref);
  }
}

TEST_CASE("nexenne::algorithm::radix_sort matches std::sort across every unsigned width") {
  auto gen{lcg{}};
  SUBCASE("uint8 (odd sizeof, copy-back path)") {
    radix_matches_std<std::uint8_t>(gen);
  }
  SUBCASE("uint16") {
    radix_matches_std<std::uint16_t>(gen);
  }
  SUBCASE("uint32") {
    radix_matches_std<std::uint32_t>(gen);
  }
  SUBCASE("uint64") {
    radix_matches_std<std::uint64_t>(gen);
  }
}

TEST_CASE("nexenne::algorithm::radix_sort handles degenerate distributions") {
  auto run{[](std::vector<std::uint32_t> data) {
    auto ref{data};
    std::ranges::sort(ref);
    alg::radix_sort(std::span<std::uint32_t>{data});
    CHECK(data == ref);
  }};
  run(std::vector<std::uint32_t>(100, 0xDEADBEEFu));              // all equal
  run(std::vector<std::uint32_t>(100, 0u));                       // all zero
  run(std::vector<std::uint32_t>(100, 0xFFFFFFFFu));              // all max
  run({0u, 1u, 2u, 3u, 4u});                                      // sorted
  run({4u, 3u, 2u, 1u, 0u});                                      // reversed
  run({0xFFFFFFFFu, 0u, 0x00FF00FFu, 0xFF00FF00u, 0x80000000u});  // spread across all bytes
}

TEST_CASE("nexenne::algorithm::radix_sort iterator overload matches span overload") {
  auto gen{lcg{}};
  auto data{random_values<std::uint32_t>(gen, 500)};
  auto by_span{data};
  alg::radix_sort(std::span<std::uint32_t>{by_span});
  alg::radix_sort(data.begin(), data.end());
  CHECK(data == by_span);
}

TEST_CASE("nexenne::algorithm sorts are constexpr-evaluable") {
  static constexpr auto counted{[] {
    auto a{std::array<std::uint16_t, 5>{4, 1, 3, 1, 0}};
    alg::counting_sort(std::span<std::uint16_t>{a}, std::uint16_t{4});
    return a;
  }()};
  static_assert(counted == std::array<std::uint16_t, 5>{0, 1, 1, 3, 4});

  // Allocating radix at compile time (transient allocation).
  static constexpr auto radixed{[] {
    auto a{std::array<std::uint32_t, 5>{50, 40, 30, 20, 10}};
    alg::radix_sort(std::span<std::uint32_t>{a});
    return a;
  }()};
  static_assert(radixed == std::array<std::uint32_t, 5>{10, 20, 30, 40, 50});

  // Heap-free scratch radix at compile time.
  static constexpr auto scratched{[] {
    auto a{std::array<std::uint8_t, 4>{200, 1, 200, 0}};
    auto s{std::array<std::uint8_t, 4>{}};
    alg::radix_sort(std::span<std::uint8_t>{a}, std::span<std::uint8_t>{s});
    return a;
  }()};
  static_assert(scratched == std::array<std::uint8_t, 4>{0, 1, 200, 200});
  CHECK(scratched[0] == 0);
}

TEST_CASE("nexenne::algorithm integer sorts match std::sort on a large random buffer") {
  auto gen{lcg{}};
  auto const data0{random_values<std::uint32_t>(gen, 100000)};
  auto ref{data0};
  std::ranges::sort(ref);

  auto radixed{data0};
  alg::radix_sort(std::span<std::uint32_t>{radixed});
  CHECK(radixed == ref);

  auto counted{data0};
  auto const hi{*std::ranges::max_element(counted)};
  alg::counting_sort(std::span<std::uint32_t>{counted}, hi);
  CHECK(counted == ref);
}

TEST_CASE("nexenne::algorithm::counting_sort falls back instead of overflowing the bucket count") {
  // max_value at size_t's maximum makes the bucket count (max + 1) wrap to zero;
  // the sort must fall back to a comparison sort, not write into an empty vector.
  auto data{std::vector<std::uint64_t>{5, 1, 9, 1, 0, 7, 3}};
  auto ref{data};
  std::ranges::sort(ref);
  alg::counting_sort(std::span<std::uint64_t>{data}, std::numeric_limits<std::uint64_t>::max());
  CHECK(data == ref);
}

}  // namespace
