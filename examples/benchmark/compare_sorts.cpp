/**
 * @file
 * @brief Benchmark and compare two routines, and time fresh-state work.
 *
 * Shows the three things you reach for: plain run on a repeatable routine,
 * run_with_setup when each call must start from fresh state, and compare to
 * report a speedup. do_not_optimize keeps the work under test from being
 * deleted by the optimiser. Timings are machine-dependent, so this prints the
 * measured summaries rather than asserting fixed numbers.
 */

#include <algorithm>
#include <cstdint>
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

  return 0;
}
