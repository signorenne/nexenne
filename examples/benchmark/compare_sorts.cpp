/**
 * @file
 * @brief Benchmark and compare two routines, and time fresh-state work.
 *
 * Shows the three things you reach for: plain run on a repeatable routine,
 * run_with_setup when each call must start from fresh state, and compare to
 * report a speedup. do_not_optimize keeps the work under test from being
 * deleted by the optimiser. Timings are machine-dependent, so this prints the
 * measured summaries rather than asserting fixed numbers.
 *
 * The last two tours go further: a custom config to trade accuracy for a faster
 * run, a throughput figure derived from the result, and the JSON dump used to
 * feed a result into CI.
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

#include <nexenne/benchmark/benchmark.hpp>

namespace {

namespace bm = nexenne::benchmark;

// A fixed pseudo-random vector to work over (no RNG dependency needed here).
auto make_data(std::size_t const n) -> std::vector<std::uint32_t> {
  auto v{std::vector<std::uint32_t>{}};
  v.reserve(n);
  auto state{std::uint32_t{0x9e3779b9}};
  for (auto i{std::size_t{0}}; i < n; ++i) {
    state = state * 1664525u + 1013904223u;  // a tiny LCG, just for varied data
    v.push_back(state);
  }
  return v;
}

}  // namespace

auto main() -> int {
  auto const data{make_data(4096)};

  // Two ways to sum the same data: a hand loop versus std::accumulate. Each is a
  // pure repeatable routine, so plain run fits; do_not_optimize keeps the sum
  // (and therefore the summing) from being optimised away.
  auto const hand{bm::run("sum: hand loop", [&] noexcept {
    auto total{std::uint64_t{0}};
    for (auto const x : data) {
      total += x;
    }
    bm::do_not_optimize(total);
  })};

  auto const stdlib{bm::run("sum: std::accumulate", [&] noexcept {
    auto const total{std::accumulate(data.begin(), data.end(), std::uint64_t{0})};
    bm::do_not_optimize(total);
  })};

  hand.print();
  stdlib.print();
  bm::compare(hand, stdlib).print();

  // Sorting mutates its input, so each iteration must start from a fresh copy.
  // run_with_setup restores the copy (untimed) before each timed sort.
  auto scratch{std::vector<std::uint32_t>{}};
  auto const sorted{bm::run_with_setup(
    "sort 4k (fresh each call)",
    [&] { scratch = data; },
    [&] noexcept {
      std::ranges::sort(scratch);
      bm::do_not_optimize(scratch);
    }
  )};
  sorted.print();

  // A custom config: fewer, shorter sample batches and no warmup for a quick,
  // lower-confidence reading. Use this when you want a fast turnaround during
  // development; keep the defaults for numbers you will quote. We also turn the
  // result into a throughput figure: the hand sum touches data.size() 4-byte
  // elements per call, so bytes_per_second reports the achieved memory rate.
  auto const quick_cfg{bm::config{
    .target_duration = std::chrono::milliseconds{20},
    .sample_count = 3,
    .min_iterations = 1,
    .warmup = false,
  }};
  auto const quick{bm::run(
    "sum: hand loop (quick cfg)",
    [&] noexcept {
      auto total{std::uint64_t{0}};
      for (auto const x : data) {
        total += x;
      }
      bm::do_not_optimize(total);
    },
    quick_cfg
  )};
  quick.print();
  auto const bytes_per_iter{data.size() * sizeof(std::uint32_t)};
  std::cout << "  -> " << quick.bytes_per_second(bytes_per_iter) / 1e9 << " GB/s scanned\n";

  // For CI: dump the result as a JSON object (name, the timing fields, and the
  // raw per-sample means) so a dashboard can ingest it without scraping text.
  std::cout << "json: ";
  quick.to_json(std::cout);
  std::cout << '\n';

  return 0;
}
