/**
 * @file
 * @brief A guided tour of nexenne::benchmark through one realistic question:
 *        what is the fastest way to look up an integer key among a few hundred?
 *
 * We compare three containers on the same task, a point lookup of 64 random
 * keys: std::map (a red-black tree), std::unordered_map (a hash table), and a
 * sorted std::vector searched with std::lower_bound (a "flat map"). All three
 * answer the same query; the question is only which is fastest for this size,
 * and the runner exists to answer it honestly.
 *
 * What the runner does for us, and why each part matters:
 *
 *   - Auto-tuning: a calibration call measures the single-call cost, then the
 *     runner picks an iteration count that fills a time budget (default 100 ms).
 *     A single lookup is a handful of nanoseconds, far below the clock's
 *     resolution, so timing one call is meaningless; timing a million and
 *     dividing is not.
 *   - Warmup: one batch runs and is discarded first, so the caches are warm and
 *     the CPU has ramped to its boost frequency before the timed batches. The
 *     first run is always an outlier; we do not want it in the statistics.
 *   - Repetition and statistics: ten independent batches give a distribution,
 *     not a single number. We read the median (robust to a scheduling hiccup)
 *     and the coefficient of variation (cv = stddev / mean), which tells us
 *     whether to trust the result at all. A high cv means noise dominated.
 *   - do_not_optimize: the lookup result is fed to do_not_optimize so the
 *     optimiser cannot notice we discard it and delete the whole search. Without
 *     it the benchmark would time an empty loop and report a fiction.
 *
 * Timings are machine-dependent, so this prints the measured summaries and a
 * data-driven verdict rather than asserting fixed numbers. Read it top to
 * bottom.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <print>
#include <unordered_map>
#include <vector>

#include <nexenne/benchmark/benchmark.hpp>

namespace bench = nexenne::benchmark;

namespace {

constexpr std::size_t key_count{256};   // entries in each container
constexpr std::size_t probe_count{64};  // lookups timed per benchmark iteration

// A tiny LCG so the keys and probes are varied but reproducible, no RNG
// dependency and the same data every run.
auto make_keys(std::size_t const n, std::uint32_t seed) -> std::vector<std::uint32_t> {
  auto v{std::vector<std::uint32_t>{}};
  v.reserve(n);
  for (auto i{std::size_t{0}}; i < n; ++i) {
    seed = seed * 1664525u + 1013904223u;
    v.push_back(seed);
  }
  return v;
}

}  // namespace

auto main() -> int {
  // Build the shared dataset once. Every container holds the same key -> value
  // mapping; the probe list is a fixed set of keys we know are present, so each
  // benchmark does identical work and only the container differs.
  auto const keys{make_keys(key_count, 0x9e3779b9u)};
  auto const probes{make_keys(probe_count, 0x12345678u)};

  auto tree{std::map<std::uint32_t, std::uint32_t>{}};
  auto hash{std::unordered_map<std::uint32_t, std::uint32_t>{}};
  auto flat{std::vector<std::pair<std::uint32_t, std::uint32_t>>{}};
  for (auto i{std::size_t{0}}; i < keys.size(); ++i) {
    auto const k{keys[i]};
    auto const val{static_cast<std::uint32_t>(i)};
    tree.emplace(k, val);
    hash.emplace(k, val);
    flat.emplace_back(k, val);
  }
  // The flat map must be sorted by key for the binary search below to be valid.
  std::ranges::sort(flat, {}, &std::pair<std::uint32_t, std::uint32_t>::first);

  // The probe keys we actually look up. A random 32-bit probe almost never hits a
  // 256-key set, so in practice this falls back to a known-present key for nearly
  // every lookup; the point is only that every lookup finds something, keeping the
  // work uniform and non-trivial across the three implementations.
  auto lookups{std::vector<std::uint32_t>{}};
  for (auto const p : probes) {
    lookups.push_back(tree.contains(p) ? p : keys[p % keys.size()]);
  }

  std::println("== Lookup of {} keys in a {}-entry container ==", lookups.size(), key_count);

  // Benchmark 1: std::map. Each lookup walks the red-black tree, O(log n) with a
  // pointer chase per level, so cache misses dominate at this size. We fold every
  // found value into a running sum and hand the sum to do_not_optimize, so the
  // optimiser must perform every lookup.
  auto const r_tree{bench::run("std::map (rb-tree)", [&] noexcept {
    auto sum{std::uint32_t{0}};
    for (auto const k : lookups) {
      if (auto const it{tree.find(k)}; it != tree.end()) {
        sum += it->second;
      }
    }
    bench::do_not_optimize(sum);
  })};

  // Benchmark 2: std::unordered_map. O(1) average, one hash plus a bucket walk,
  // but the buckets are heap nodes so a miss in the cache still hurts.
  auto const r_hash{bench::run("std::unordered_map (hash)", [&] noexcept {
    auto sum{std::uint32_t{0}};
    for (auto const k : lookups) {
      if (auto const it{hash.find(k)}; it != hash.end()) {
        sum += it->second;
      }
    }
    bench::do_not_optimize(sum);
  })};

  // Benchmark 3: flat map. A binary search over one contiguous array: O(log n)
  // comparisons, but the probes stay in a single cache-friendly block, which can
  // make a flat map competitive at small sizes despite the same big-O as the
  // tree. Whether it actually wins depends on the data and the machine - read the
  // measured verdict below rather than assuming.
  auto const r_flat{bench::run("flat map (sorted vector)", [&] noexcept {
    auto sum{std::uint32_t{0}};
    for (auto const k : lookups) {
      auto const it{
        std::ranges::lower_bound(flat, k, {}, &std::pair<std::uint32_t, std::uint32_t>::first)
      };
      if (it != flat.end() && it->first == k) {
        sum += it->second;
      }
    }
    bench::do_not_optimize(sum);
  })};

  // The summaries: median and mean +/- stddev, the cv as a noise gauge, and the
  // min-max range, each auto-scaled to ns / us / ms.
  std::println("== Per-implementation timing ==");
  r_tree.print();
  r_hash.print();
  r_flat.print();

  // Throughput is often the clearer number: lookups per second derived from the
  // mean, given probe_count lookups per timed iteration.
  std::println("== Throughput (lookups/sec, from the mean) ==");
  std::println("  std::map           {:>14.3e}", r_tree.items_per_second(probe_count));
  std::println("  std::unordered_map {:>14.3e}", r_hash.items_per_second(probe_count));
  std::println("  flat map           {:>14.3e}", r_flat.items_per_second(probe_count));

  // compare() reports the candidate against a baseline as a speedup factor.
  // There is no statistical test, so we read the cv first: above ~10% the run
  // was too noisy to trust and the verdict is meaningless.
  std::println("== Verdict (flat map vs the trees) ==");
  bench::compare(r_tree, r_flat).print();
  bench::compare(r_hash, r_flat).print();

  auto const worst_cv{std::max({r_tree.cv(), r_hash.cv(), r_flat.cv()})};
  if (worst_cv > 0.10) {
    std::println(
      "  note: worst cv is {:.1f}% - noisy, re-run on a quiet machine", worst_cv * 100.0
    );
  } else {
    std::println("  cv under 10% across the board, the ranking is trustworthy");
  }

  std::println("\nThat is the module in one experiment: auto-tuned iteration");
  std::println("counts, a discarded warmup, ten-sample statistics with a noise");
  std::println("gauge, do_not_optimize keeping the search alive, and a speedup");
  std::println("verdict - all from three run() calls and a compare().");
  return 0;
}
