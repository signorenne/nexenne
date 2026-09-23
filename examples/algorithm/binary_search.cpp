/**
 * @file
 * @brief Example: the nexenne::algorithm binary-search variants.
 *
 * Shows the three index-returning searches over sorted ranges: find_sorted
 * (general), exponential_search (galloping, strong near the front), and
 * interpolation_search (fast on uniformly distributed numeric data), plus their
 * edge cases: a miss, an empty range, duplicate keys, and a flat span. Each
 * returns a found_index (an optional index) usable for direct array access.
 *
 * The program walks four steps over the primes below 30:
 *
 * 1. find_sorted, the general O(log N) search, hits 13 and misses 14.
 * 2. exponential_search gallops from the front, so its cost scales with the
 *    key's distance from index 0 rather than with N; interpolation_search
 *    predicts the probe from the key's position in the value range, O(log log
 *    N) on uniform data.
 * 3. The returned index addresses the array directly, with no iterator
 *    round-trip.
 * 4. Edge cases: an empty range is always a miss, never a crash; with duplicate
 *    keys find_sorted returns the first match (it is built on lower_bound), the
 *    stable choice for an equal_range follow-up; a flat span (equal endpoints)
 *    degrades interpolation_search to a direct equality check instead of a
 *    division by zero.
 */

#include <array>
#include <cstdio>
#include <vector>

#include <nexenne/algorithm/binary_search.hpp>

namespace alg = nexenne::algorithm;

namespace {

auto report(char const* const name, alg::found_index const r) -> void {
  if (r.has_value()) {
    std::printf("  %-30s -> index %zu\n", name, *r);
  } else {
    std::printf("  %-30s -> not found\n", name);
  }
}

}  // namespace

auto main() -> int {
  constexpr auto primes{std::array{2, 3, 5, 7, 11, 13, 17, 19, 23, 29}};

  std::puts("searching the primes [2 3 5 7 11 13 17 19 23 29]:");
  report("find_sorted(13)", alg::find_sorted(primes, 13));
  report("find_sorted(14)  (miss)", alg::find_sorted(primes, 14));

  report("exponential_search(3)", alg::exponential_search(primes, 3));
  report("exponential_search(4)  (miss)", alg::exponential_search(primes, 4));

  report("interpolation_search(29)", alg::interpolation_search(primes, 29));
  report("interpolation_search(0)  (miss)", alg::interpolation_search(primes, 0));

  if (auto const at{alg::find_sorted(primes, 17)}) {
    std::printf("data[%zu] == %d\n", *at, primes[*at]);
  }

  constexpr auto empty{std::array<int, 0>{}};
  report("find_sorted on empty range", alg::find_sorted(empty, 1));

  auto const dups{std::vector<int>{1, 4, 4, 4, 9}};
  report("find_sorted(4) in [1 4 4 4 9]", alg::find_sorted(dups, 4));

  constexpr auto flat{std::array{5, 5, 5, 5}};
  report("interpolation_search(5) flat", alg::interpolation_search(flat, 5));
  report("interpolation_search(6) flat", alg::interpolation_search(flat, 6));
  return 0;
}
