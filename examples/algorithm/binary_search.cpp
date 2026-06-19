/**
 * @file
 * @brief Example: the nexenne::algorithm binary-search variants.
 *
 * Shows the three index-returning searches over a sorted array: find_sorted
 * (general), exponential_search (galloping, strong near the front), and
 * interpolation_search (fast on uniformly distributed numeric data).
 */

#include <array>
#include <cstdio>

#include <nexenne/algorithm/binary_search.hpp>

namespace alg = nexenne::algorithm;

auto main() -> int {
  // A sorted array; each search returns a zero-based index, or empty on a miss.
  constexpr auto data{std::array{2, 3, 5, 7, 11, 13, 17, 19, 23, 29}};

  auto const report{[](char const* const name, alg::found_index const r) {
    if (r.has_value()) {
      std::printf("  %-22s -> index %zu\n", name, *r);
    } else {
      std::printf("  %-22s -> not found\n", name);
    }
  }};

  std::puts("searching the primes [2 3 5 7 11 13 17 19 23 29]:");
  report("find_sorted(13)", alg::find_sorted(data, 13));
  report("find_sorted(14)", alg::find_sorted(data, 14));
  report("exponential_search(3)", alg::exponential_search(data, 3));
  report("interpolation_search(29)", alg::interpolation_search(data, 29));

  // The index can drive a direct array access, no iterator round-trip.
  if (auto const at{alg::find_sorted(data, 17)}) {
    std::printf("data[%zu] == %d\n", *at, data[*at]);
  }
  return 0;
}
