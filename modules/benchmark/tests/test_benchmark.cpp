/**
 * @file
 * @brief Tests for nexenne::benchmark (runner, statistics, comparison).
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <format>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nexenne/benchmark/benchmark.hpp>
#include <nexenne/benchmark/do_not_optimize.hpp>
#include <nexenne/serialization/json/parse.hpp>

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

TEST_CASE("nexenne::benchmark::result computes exact statistics for a known sample set") {
  // Samples 2, 4, 4, 4, 5, 5, 7, 9: mean 5, population stddev 2, sum of squared
  // deviations 32, so the Bessel-corrected sample stddev is sqrt(32 / 7).
  auto const r{
    bm::result{std::string{"known"}, std::vector<double>{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}, 8}
  };
  CHECK(r.mean() == doctest::Approx{5.0});
  CHECK(r.min() == doctest::Approx{2.0});
  CHECK(r.max() == doctest::Approx{9.0});
  CHECK(r.median() == doctest::Approx{4.5});  // even count: (4 + 5) / 2
  // Bessel-corrected (n-1) sample stddev: sqrt(32 / 7).
  CHECK(r.stddev() == doctest::Approx{std::sqrt(32.0 / 7.0)});
  CHECK(r.cv() == doctest::Approx{std::sqrt(32.0 / 7.0) / 5.0});
  CHECK(r.name() == "known");
  CHECK(r.total_iterations() == 8);
  // The defining relationships hold for any non-empty sample set.
  CHECK(r.min() <= r.mean());
  CHECK(r.mean() <= r.max());
  CHECK(r.min() <= r.median());
  CHECK(r.median() <= r.max());
}

TEST_CASE("nexenne::benchmark::result median picks the middle of an odd sample count") {
  auto const r{bm::result{std::string{"odd"}, std::vector<double>{9.0, 1.0, 5.0}, 3}};
  CHECK(r.median() == doctest::Approx{5.0});  // sorted: 1, 5, 9
  CHECK(r.mean() == doctest::Approx{5.0});
  CHECK(r.min() == doctest::Approx{1.0});
  CHECK(r.max() == doctest::Approx{9.0});
}

TEST_CASE("nexenne::benchmark::result with a single sample has zero spread") {
  // stddev/cv use (n - 1) normalisation, so a lone sample reports zero spread,
  // and min == median == mean == max == that one value.
  auto const r{bm::result{std::string{"one"}, std::vector<double>{42.0}, 1}};
  CHECK(r.mean() == doctest::Approx{42.0});
  CHECK(r.median() == doctest::Approx{42.0});
  CHECK(r.min() == doctest::Approx{42.0});
  CHECK(r.max() == doctest::Approx{42.0});
  CHECK(r.stddev() == 0.0);
  CHECK(r.cv() == 0.0);
}

TEST_CASE("nexenne::benchmark::run with a single sample yields exactly one mean") {
  auto cfg{fast_cfg};
  cfg.sample_count = 1;
  auto const r{bm::run(
    "single sample",
    [] noexcept {
      auto v{int{}};
      bm::do_not_optimize(v);
    },
    cfg
  )};
  CHECK(r.samples().size() == 1);
  CHECK(r.stddev() == 0.0);             // one sample -> no spread
  CHECK(r.mean() == r.samples()[0]);    // mean of one is the sample itself
  CHECK(r.median() == r.samples()[0]);  // median of one likewise
}

TEST_CASE("nexenne::benchmark::run handles a no-op body with a positive iteration count") {
  auto const r{bm::run("empty body", [] noexcept {}, fast_cfg)};
  CHECK(r.samples().size() == fast_cfg.sample_count);
  CHECK(r.total_iterations() > 0);  // calibration chose a sensible count
  CHECK(std::isfinite(r.mean()));
  CHECK(r.mean() >= 0.0);
}

TEST_CASE("nexenne::benchmark::run picks a large default count when a call is unmeasurable") {
  // An empty body times as 0 ns in calibration, so the runner falls back to its
  // 1024-iteration default; with no warmup that is exactly the per-sample count.
  auto cfg{fast_cfg};
  cfg.sample_count = 1;
  cfg.min_iterations = 1;
  cfg.warmup = false;
  auto const r{bm::run("unmeasurable", [] noexcept {}, cfg)};
  CHECK(r.total_iterations() >= 1024);
}

TEST_CASE("nexenne::benchmark::run honours min_iterations exactly with no warmup") {
  // A sleeping body keeps the calibrated count at the min_iterations floor (the
  // target budget admits only one iteration), so total_iterations is exact.
  auto cfg{bm::config{
    .target_duration = std::chrono::nanoseconds{1},
    .sample_count = 2,
    .min_iterations = 3,
    .warmup = false,
  }};
  auto const r{bm::run(
    "exact-floor", [] { std::this_thread::sleep_for(std::chrono::microseconds{20}); }, cfg
  )};
  CHECK(r.total_iterations() == 3 * 2);  // min_iterations * sample_count, no warmup
}

TEST_CASE("nexenne::benchmark::do_not_optimize leaves the value unchanged for many types") {
  struct point {
    int x;
    double y;
  };

  auto scalar{int{7}};
  bm::do_not_optimize(scalar);
  CHECK(scalar == 7);

  auto floating{double{3.5}};
  bm::do_not_optimize(floating);
  CHECK(floating == doctest::Approx{3.5});

  auto aggregate{point{.x = 1, .y = 2.0}};
  bm::do_not_optimize(aggregate);
  CHECK(aggregate.x == 1);
  CHECK(aggregate.y == doctest::Approx{2.0});

  auto target{int{99}};
  auto* ptr{&target};
  bm::do_not_optimize(ptr);
  CHECK(ptr == &target);
  CHECK(*ptr == 99);

  // The const overload accepts an rvalue / read-only value too.
  bm::do_not_optimize(int{123});
  bm::do_not_optimize(point{.x = 4, .y = 5.0});
}

TEST_CASE("nexenne::benchmark::clobber_memory does not disturb surrounding state") {
  auto buffer{std::vector<int>{1, 2, 3}};
  buffer[1] = 20;
  bm::clobber_memory();  // pretend the store escaped
  CHECK(buffer[0] == 1);
  CHECK(buffer[1] == 20);
  CHECK(buffer[2] == 3);
}

TEST_CASE("nexenne::benchmark::run_with_setup makes the setup's effect visible to the body") {
  auto state{int{0}};
  auto observed_zero_each_time{true};
  auto cfg{fast_cfg};
  cfg.sample_count = 2;
  cfg.min_iterations = 4;
  auto const r{bm::run_with_setup(
    "fresh state",
    [&] noexcept { state = 0; },  // reset before every timed call
    [&] noexcept {
      if (state != 0) {
        observed_zero_each_time = false;  // the body always sees the reset state
      }
      ++state;  // body mutates; setup must undo it next time
      bm::do_not_optimize(state);
    },
    cfg
  )};
  CHECK(observed_zero_each_time);  // setup reset was visible on every iteration
  CHECK(r.samples().size() == 2);
}

TEST_CASE("nexenne::benchmark::compare of a result against itself is unity") {
  auto const r{bm::result{std::string{"self"}, std::vector<double>{10.0, 12.0, 14.0}, 3}};
  auto const c{bm::compare(r, r)};
  CHECK(c.ratio() == doctest::Approx{1.0});
  CHECK(c.speedup() == doctest::Approx{1.0});
}

TEST_CASE("nexenne::benchmark::comparison ratio and speedup are reciprocals") {
  auto const baseline{bm::result{std::string{"base"}, std::vector<double>{100.0}, 1}};
  auto const candidate{bm::result{std::string{"cand"}, std::vector<double>{25.0}, 1}};
  auto const c{bm::compare(baseline, candidate)};
  CHECK(c.ratio() == doctest::Approx{0.25});   // candidate / baseline
  CHECK(c.speedup() == doctest::Approx{4.0});  // baseline / candidate
  CHECK(c.ratio() * c.speedup() == doctest::Approx{1.0});
  CHECK(c.speedup() > 0.0);
  CHECK(std::isfinite(c.speedup()));
}

TEST_CASE("nexenne::benchmark::comparison print preserves both labels and names the direction") {
  auto const baseline{bm::result{std::string{"alpha-label"}, std::vector<double>{100.0}, 1}};
  auto const candidate{bm::result{std::string{"beta-label"}, std::vector<double>{50.0}, 1}};
  auto ss{std::stringstream{}};
  bm::compare(baseline, candidate).print(ss);
  auto const s{ss.str()};
  CHECK(s.find("alpha-label") != std::string::npos);
  CHECK(s.find("beta-label") != std::string::npos);
  CHECK(s.find("2.00x faster") != std::string::npos);  // candidate is twice as fast
}

TEST_CASE("nexenne::benchmark::comparison names the slower direction when the candidate regresses"
) {
  auto const baseline{bm::result{std::string{"base"}, std::vector<double>{50.0}, 1}};
  auto const candidate{bm::result{std::string{"cand"}, std::vector<double>{100.0}, 1}};
  auto ss{std::stringstream{}};
  bm::compare(baseline, candidate).print(ss);
  CHECK(ss.str().find("2.00x slower") != std::string::npos);
}

TEST_CASE("nexenne::benchmark::result to_json emits every documented key in a fixed order") {
  auto const r{bm::result{std::string{"keys"}, std::vector<double>{1.0, 2.0, 3.0}, 3}};
  auto ss{std::stringstream{}};
  r.to_json(ss);
  auto const s{ss.str()};
  // Keys appear in the documented, stable order.
  auto const i_name{s.find("\"name\"")};
  auto const i_mean{s.find("\"mean_ns\"")};
  auto const i_median{s.find("\"median_ns\"")};
  auto const i_stddev{s.find("\"stddev_ns\"")};
  auto const i_min{s.find("\"min_ns\"")};
  auto const i_max{s.find("\"max_ns\"")};
  auto const i_cv{s.find("\"cv\"")};
  auto const i_samples{s.find("\"samples\"")};
  for (auto const idx : {i_name, i_mean, i_median, i_stddev, i_min, i_max, i_cv, i_samples}) {
    CHECK(idx != std::string::npos);
  }
  CHECK(i_name < i_mean);
  CHECK(i_mean < i_median);
  CHECK(i_median < i_stddev);
  CHECK(i_stddev < i_min);
  CHECK(i_min < i_max);
  CHECK(i_max < i_cv);
  CHECK(i_cv < i_samples);
}

TEST_CASE("nexenne::benchmark::result to_json round-trips through the JSON parser") {
  namespace json = nexenne::serialization::json;
  auto const r{bm::result{std::string{"round trip"}, std::vector<double>{2.0, 4.0, 6.0}, 12}};
  auto ss{std::stringstream{}};
  r.to_json(ss);
  auto const s{ss.str()};

  auto const parsed{json::parse(s)};
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->is_object());

  auto const& obj{*parsed};
  CHECK(obj["name"].as_string().value() == "round trip");
  CHECK(obj["mean_ns"].as_float().value() == doctest::Approx{4.0});  // (2+4+6)/3
  CHECK(obj["median_ns"].as_float().value() == doctest::Approx{4.0});
  CHECK(obj["min_ns"].as_float().value() == doctest::Approx{2.0});
  CHECK(obj["max_ns"].as_float().value() == doctest::Approx{6.0});

  // The samples array reflects the values in insertion order.
  auto const arr{obj["samples"].as_array()};
  REQUIRE(arr.has_value());
  auto const& samples{arr->get()};
  REQUIRE(samples.size() == 3);
  CHECK(samples[0].as_float().value() == doctest::Approx{2.0});
  CHECK(samples[1].as_float().value() == doctest::Approx{4.0});
  CHECK(samples[2].as_float().value() == doctest::Approx{6.0});
}

TEST_CASE("nexenne::benchmark::result to_json preserves full numeric precision") {
  namespace json = nexenne::serialization::json;
  // A value the default 6-digit float formatting would round; the writer must
  // round-trip it back to the same double.
  auto const value{0.123456789012345};
  auto const r{bm::result{std::string{"precise"}, std::vector<double>{value}, 1}};
  auto ss{std::stringstream{}};
  r.to_json(ss);
  auto const parsed{json::parse(ss.str())};
  REQUIRE(parsed.has_value());
  CHECK((*parsed)["mean_ns"].as_float().value() == doctest::Approx{value}.epsilon(1e-12));
  CHECK(
    (*parsed)["samples"].as_array().value().get()[0].as_float().value()
    == doctest::Approx{value}.epsilon(1e-12)
  );
}

TEST_CASE("nexenne::benchmark::result to_json escaping round-trips back to the original name") {
  namespace json = nexenne::serialization::json;
  auto const name{std::string{"a\"b\\c\nd\te"}};
  auto const r{bm::result{name, std::vector<double>{1.0}, 1}};
  auto ss{std::stringstream{}};
  r.to_json(ss);
  auto const parsed{json::parse(ss.str())};
  REQUIRE(parsed.has_value());
  CHECK((*parsed)["name"].as_string().value() == name);  // decoded back verbatim
}

TEST_CASE("nexenne::benchmark::result is formattable via std::format") {
  auto const r{bm::result{std::string{"fmt-name"}, std::vector<double>{5.0, 7.0}, 2}};
  auto const s{std::format("{}", r)};
  CHECK(s == r.to_string());  // formatter delegates to to_string
  CHECK(s.find("fmt-name") != std::string::npos);
  CHECK(s.find("median:") != std::string::npos);
}

TEST_CASE("nexenne::benchmark::comparison is formattable via std::format") {
  auto const baseline{bm::result{std::string{"b"}, std::vector<double>{100.0}, 1}};
  auto const candidate{bm::result{std::string{"c"}, std::vector<double>{50.0}, 1}};
  auto const c{bm::compare(baseline, candidate)};
  auto const s{std::format("{}", c)};
  CHECK(s == c.to_string());
  CHECK(s.find("faster") != std::string::npos);
}

}  // namespace
