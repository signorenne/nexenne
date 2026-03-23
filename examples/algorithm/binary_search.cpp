/**
 * @file
 * @brief Example: the nexenne::algorithm binary-search variants.
 *
 * Shows the three index-returning searches over sorted ranges: find_sorted
 * (general), exponential_search (galloping, strong near the front), and
 * interpolation_search (fast on uniformly distributed numeric data), plus their
 * edge cases: a miss, an empty range, duplicate keys, and a flat span. Each
 * returns a found_index (an optional index) usable for direct array access.
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
  // A sorted array; each search returns a zero-based index, or empty on a miss.
  constexpr auto primes{std::array{2, 3, 5, 7, 11, 13, 17, 19, 23, 29}};

  std::puts("searching the primes [2 3 5 7 11 13 17 19 23 29]:");
  report("find_sorted(13)", alg::find_sorted(primes, 13));
  report("find_sorted(14)  (miss)", alg::find_sorted(primes, 14));

  // exponential_search gallops from the front: cost scales with the target's
  // distance from index 0, not with N, so it wins when the key sits near the
  // start of a large range. Here 3 is the second element; 4 is absent.
  report("exponential_search(3)", alg::exponential_search(primes, 3));
  report("exponential_search(4)  (miss)", alg::exponential_search(primes, 4));

  // interpolation_search predicts the probe from the key's linear position in the
  // value range: O(log log N) on uniform data, here finding the last element.
  report("interpolation_search(29)", alg::interpolation_search(primes, 29));
  report("interpolation_search(0)  (miss)", alg::interpolation_search(primes, 0));

  // The returned index drives a direct array access, no iterator round-trip.
  if (auto const at{alg::find_sorted(primes, 17)}) {
    std::printf("data[%zu] == %d\n", *at, primes[*at]);
  }

  // Edge cases. An empty range is always a miss, never a crash.
  constexpr auto empty{std::array<int, 0>{}};
  report("find_sorted on empty range", alg::find_sorted(empty, 1));

  // With duplicate keys, find_sorted returns the first matching index (it is
  // built on lower_bound), the stable choice for an equal_range follow-up.
  auto const dups{std::vector<int>{1, 4, 4, 4, 9}};
  report("find_sorted(4) in [1 4 4 4 9]", alg::find_sorted(dups, 4));

  // interpolation_search needs arithmetic values but tolerates a flat span (equal
  // endpoints) without dividing by zero: it degrades to a direct equality check.
  constexpr auto flat{std::array{5, 5, 5, 5}};
  report("interpolation_search(5) flat", alg::interpolation_search(flat, 5));
  report("interpolation_search(6) flat", alg::interpolation_search(flat, 6));
  return 0;
}
