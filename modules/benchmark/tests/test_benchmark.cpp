/**
 * @file
 * @brief Tests for nexenne::benchmark (runner, statistics, comparison).
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <thread>

#include <nexenne/benchmark/benchmark.hpp>
#include <nexenne/benchmark/do_not_optimize.hpp>

namespace {

namespace bm = nexenne::benchmark;

// A fast config for the suite: we do not want each case to spend hundreds of ms
// calibrating and sampling.
constexpr auto fast_cfg{bm::config{
  .target_duration = std::chrono::microseconds{500},
  .sample_count = 3,
  .min_iterations = 1,
  .warmup = false,
}};

TEST_CASE("nexenne::benchmark::do_not_optimize keeps an unused value from being elided") {
  auto const r{bm::run(
    "dce-protected",
    [] noexcept {
      auto v{int{42}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  CHECK(r.mean() >= 0.0);
  CHECK(r.samples().size() == fast_cfg.sample_count);
}

TEST_CASE("nexenne::benchmark::clobber_memory is callable") {
  bm::clobber_memory();
  CHECK(true);  // smoke test: compiles and executes
}

TEST_CASE("nexenne::benchmark::run produces the requested number of samples") {
  auto cfg{fast_cfg};
  cfg.sample_count = 7;
  auto const r{bm::run(
    "seven samples",
    [] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    cfg
  )};
  CHECK(r.samples().size() == 7);
}

TEST_CASE("nexenne::benchmark::result statistics are internally consistent") {
  auto const r{bm::run(
    "constant work",
    [] noexcept {
      auto v{int{1}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  CHECK(r.mean() >= r.min());
  CHECK(r.mean() <= r.max());
  CHECK(r.median() >= r.min());
  CHECK(r.median() <= r.max());
  CHECK(r.stddev() >= 0.0);
  CHECK(r.cv() >= 0.0);
}

TEST_CASE("nexenne::benchmark::run measures a difference between cheap and expensive work") {
  auto cfg{bm::config{
    .target_duration = std::chrono::milliseconds{2},
    .sample_count = 3,
    .min_iterations = 1,
    .warmup = false,
  }};
  auto const cheap{bm::run(
    "cheap",
    [] noexcept {
      auto sum{0};
      for (auto i{0}; i < 10; ++i) {
        sum += i;
      }
      bm::do_not_optimize(sum);
    },
    fast_cfg
  )};
  auto const expensive{bm::run(
    "expensive (sleep)", [] { std::this_thread::sleep_for(std::chrono::microseconds{100}); }, cfg
  )};
  CHECK(expensive.mean() > cheap.mean());
}

TEST_CASE("nexenne::benchmark::compare reports the speedup direction correctly") {
  auto cfg{bm::config{
    .target_duration = std::chrono::milliseconds{2},
    .sample_count = 3,
    .min_iterations = 1,
    .warmup = false,
  }};
  auto const fast{bm::run(
    "fast",
    [] noexcept {
      auto v{int{1}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  auto const slow{bm::run(
    "slow", [] { std::this_thread::sleep_for(std::chrono::microseconds{50}); }, cfg
  )};
  auto const c{bm::compare(slow, fast)};  // baseline = slow, candidate = fast
  CHECK(c.speedup() > 1.0);               // candidate is faster
  CHECK(c.ratio() < 1.0);                 // candidate / baseline < 1
}

TEST_CASE("nexenne::benchmark::result print produces non-empty labelled output") {
  auto const r{bm::run(
    "printable",
    [] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  auto ss{std::stringstream{}};
  r.print(ss);
  auto const s{ss.str()};
  CHECK_FALSE(s.empty());
  CHECK(s.find("printable") != std::string::npos);
  CHECK(s.find("median:") != std::string::npos);
}

TEST_CASE("nexenne::benchmark::result to_json emits a JSON object with the expected keys") {
  auto const r{bm::run(
    "json-test",
    [] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  auto ss{std::stringstream{}};
  r.to_json(ss);
  auto const s{ss.str()};
  CHECK(s.front() == '{');
  CHECK(s.back() == '}');
  CHECK(s.find("\"name\":\"json-test\"") != std::string::npos);
  CHECK(s.find("\"mean_ns\":") != std::string::npos);
  CHECK(s.find("\"samples\":[") != std::string::npos);
}

TEST_CASE("nexenne::benchmark::comparison print writes both results and the speedup line") {
  auto const a{bm::run(
    "baseline",
    [] noexcept {
      auto v{int{1}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  auto const b{bm::run(
    "candidate",
    [] noexcept {
      auto v{int{2}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  auto ss{std::stringstream{}};
  bm::compare(a, b).print(ss);
  auto const s{ss.str()};
  CHECK(s.find("baseline") != std::string::npos);
  CHECK(s.find("candidate") != std::string::npos);
  CHECK(s.find("candidate is") != std::string::npos);
}

TEST_CASE("nexenne::benchmark::run honours an explicit min_iterations") {
  auto cfg{fast_cfg};
  cfg.min_iterations = 100;
  auto const r{bm::run(
    "min-iters",
    [] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    cfg
  )};
  CHECK(r.total_iterations() >= 100 * cfg.sample_count);
}

TEST_CASE("nexenne::benchmark::run with min_iterations 0 still yields finite timings") {
  // Regression: a zero min_iterations with a target shorter than a single call
  // could leave iters_per_sample at 0, making each per-batch mean NaN. The
  // runner clamps the divisor to at least one.
  auto cfg{fast_cfg};
  cfg.min_iterations = 0;
  auto const r{bm::run(
    "zero-min-iters",
    [] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    cfg
  )};
  CHECK(r.samples().size() == cfg.sample_count);
  CHECK(std::isfinite(r.mean()));
  CHECK(r.mean() >= 0.0);
  for (auto const m : r.samples()) {
    CHECK(std::isfinite(m));
  }
}

TEST_CASE("nexenne::benchmark::result items_per_second scales with the item count") {
  auto const r{bm::run(
    "throughput",
    [] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  if (r.mean() > 0.0) {
    auto const ips_1{r.items_per_second(1)};
    auto const ips_10{r.items_per_second(10)};
    CHECK(ips_1 > 0.0);
    CHECK(ips_10 == doctest::Approx{ips_1 * 10.0}.epsilon(1e-9));
  }
}

TEST_CASE("nexenne::benchmark::result bytes_per_second mirrors items_per_second") {
  auto const r{bm::run(
    "bytes",
    [] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  if (r.mean() > 0.0) {
    auto const bps{r.bytes_per_second(1024)};
    auto const ips{r.items_per_second(1024)};
    CHECK(bps == doctest::Approx{ips}.epsilon(1e-9));
  }
}

TEST_CASE("nexenne::benchmark::result throughput helpers return 0 on a zero-mean result") {
  auto const empty{bm::result{}};
  CHECK(empty.items_per_second(100) == 0.0);
  CHECK(empty.bytes_per_second(100) == 0.0);
  CHECK(empty.mean() == 0.0);
  CHECK(empty.median() == 0.0);
  CHECK(empty.stddev() == 0.0);
  CHECK(empty.cv() == 0.0);
}

TEST_CASE("nexenne::benchmark::run_with_setup calls setup before each timed iteration") {
  auto setup_count{0};
  auto bench_count{0};
  auto cfg{fast_cfg};
  cfg.sample_count = 2;
  cfg.min_iterations = 5;
  auto const r{bm::run_with_setup(
    "setup test",
    [&] noexcept { ++setup_count; },
    [&] noexcept {
      ++bench_count;
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    cfg
  )};
  CHECK(setup_count == bench_count);  // one setup per timed call, calibration included
  CHECK(r.samples().size() == 2);
}

TEST_CASE("nexenne::benchmark::result to_json escapes special characters in the name") {
  // A name with a quote, backslash, and newline must stay valid JSON: the
  // specials are escaped and no raw control char leaks into the output.
  auto const r{bm::result{std::string{"a\"b\\c\nd"}, std::vector<double>{1.0, 2.0}, 2}};
  auto ss{std::stringstream{}};
  r.to_json(ss);
  auto const s{ss.str()};
  CHECK(s.front() == '{');
  CHECK(s.back() == '}');
  CHECK(s.find('\n') == std::string::npos);    // the name's newline was escaped, not literal
  CHECK(s.find("\\\"") != std::string::npos);  // escaped quote
  CHECK(s.find("\\\\") != std::string::npos);  // escaped backslash
  CHECK(s.find("\\n") != std::string::npos);   // escaped newline
}

TEST_CASE("nexenne::benchmark::run tolerates a negative target_duration without UB") {
  // Regression: target_ns / single_ns is negative here; the old code cast that
  // straight to size_t (UB). The runner now floors it to the min_iterations.
  auto cfg{bm::config{
    .target_duration = std::chrono::nanoseconds{-1000},
    .sample_count = 2,
    .min_iterations = 1,
    .warmup = false,
  }};
  // A measurable body so calibration records single_ns > 0 and the ratio path runs.
  auto const r{bm::run(
    "negative target", [] { std::this_thread::sleep_for(std::chrono::microseconds{20}); }, cfg
  )};
  CHECK(r.samples().size() == 2);
  CHECK(std::isfinite(r.mean()));
}

TEST_CASE("nexenne::benchmark::comparison reports speedup unavailable for a zero-mean result") {
  auto const empty{bm::result{}};  // mean() == 0
  auto const real{bm::run(
    "real",
    [] noexcept {
      auto v{int{1}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  auto ss{std::stringstream{}};
  bm::compare(empty, real).print(ss);  // zero baseline mean -> no finite speedup
  auto const s{ss.str()};
  CHECK(s.find("unavailable") != std::string::npos);
  CHECK(s.find("inf") == std::string::npos);  // never prints inf
}

TEST_CASE("nexenne::benchmark::run_with_setup excludes the setup cost from the timing") {
  auto cfg{bm::config{
    .target_duration = std::chrono::milliseconds{5},
    .sample_count = 2,
    .min_iterations = 1,
    .warmup = false,
  }};
  auto const r{bm::run_with_setup(
    "long setup, fast bench",
    [&] { std::this_thread::sleep_for(std::chrono::milliseconds{1}); },
    [&] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    cfg
  )};
  CHECK(r.mean() < 1e5);  // far below the 1 ms (1e6 ns) setup cost
}

}  // namespace
