/**
 * @file
 * @brief Tests for nexenne::benchmark (runner, statistics, comparison).
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nexenne/benchmark/benchmark.hpp>
#include <nexenne/benchmark/do_not_optimize.hpp>
#include <nexenne/benchmark/format.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/serialization/json/parse.hpp>

namespace {

namespace bm = nexenne::benchmark;

/// @brief Fast config, so no case spends hundreds of ms calibrating and sampling.
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
  CHECK(true);
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
  auto const slow{
    bm::run("slow", [] { std::this_thread::sleep_for(std::chrono::microseconds{50}); }, cfg)
  };
  auto const c{bm::compare(slow, fast)};
  CHECK(c.speedup() > 1.0);
  CHECK(c.ratio() < 1.0);
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
  CHECK(setup_count == bench_count);
  CHECK(r.samples().size() == 2);
}

TEST_CASE("nexenne::benchmark::result to_json escapes special characters in the name") {
  auto const r{bm::result{std::string{"a\"b\\c\nd"}, std::vector<double>{1.0, 2.0}, 2}};
  auto ss{std::stringstream{}};
  r.to_json(ss);
  auto const s{ss.str()};
  CHECK(s.front() == '{');
  CHECK(s.back() == '}');
  CHECK(s.find('\n') == std::string::npos);
  CHECK(s.find("\\\"") != std::string::npos);
  CHECK(s.find("\\\\") != std::string::npos);
  CHECK(s.find("\\n") != std::string::npos);
}

TEST_CASE("nexenne::benchmark::run tolerates a negative target_duration without UB") {
  auto cfg{bm::config{
    .target_duration = std::chrono::nanoseconds{-1000},
    .sample_count = 2,
    .min_iterations = 1,
    .warmup = false,
  }};
  auto const r{bm::run(
    "negative target", [] { std::this_thread::sleep_for(std::chrono::microseconds{20}); }, cfg
  )};
  CHECK(r.samples().size() == 2);
  CHECK(std::isfinite(r.mean()));
}

TEST_CASE("nexenne::benchmark::comparison reports speedup unavailable for a zero-mean result") {
  auto const empty{bm::result{}};
  auto const real{bm::run(
    "real",
    [] noexcept {
      auto v{int{1}};
      bm::do_not_optimize(v);
    },
    fast_cfg
  )};
  auto ss{std::stringstream{}};
  bm::compare(empty, real).print(ss);
  auto const s{ss.str()};
  CHECK(s.find("unavailable") != std::string::npos);
  CHECK(s.find("inf") == std::string::npos);
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
  CHECK(r.mean() < 1e5);
}

TEST_CASE("nexenne::benchmark::result computes exact statistics for a known sample set") {
  // Mean 5; squared deviations sum to 32, so the Bessel-corrected stddev is sqrt(32 / 7).
  auto const r{
    bm::result{std::string{"known"}, std::vector<double>{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}, 8}
  };
  CHECK(r.mean() == doctest::Approx{5.0});
  CHECK(r.min() == doctest::Approx{2.0});
  CHECK(r.max() == doctest::Approx{9.0});
  CHECK(r.median() == doctest::Approx{4.5});
  CHECK(r.stddev() == doctest::Approx{std::sqrt(32.0 / 7.0)});
  CHECK(r.cv() == doctest::Approx{std::sqrt(32.0 / 7.0) / 5.0});
  CHECK(r.name() == "known");
  CHECK(r.total_iterations() == 8);
  CHECK(r.min() <= r.mean());
  CHECK(r.mean() <= r.max());
  CHECK(r.min() <= r.median());
  CHECK(r.median() <= r.max());
}

TEST_CASE("nexenne::benchmark::result median picks the middle of an odd sample count") {
  auto const r{bm::result{std::string{"odd"}, std::vector<double>{9.0, 1.0, 5.0}, 3}};
  CHECK(r.median() == doctest::Approx{5.0});
  CHECK(r.mean() == doctest::Approx{5.0});
  CHECK(r.min() == doctest::Approx{1.0});
  CHECK(r.max() == doctest::Approx{9.0});
}

TEST_CASE("nexenne::benchmark::result with a single sample has zero spread") {
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
  CHECK(r.stddev() == 0.0);
  CHECK(r.mean() == r.samples()[0]);
  CHECK(r.median() == r.samples()[0]);
}

TEST_CASE("nexenne::benchmark::run handles a no-op body with a positive iteration count") {
  auto const r{bm::run("empty body", [] noexcept {}, fast_cfg)};
  CHECK(r.samples().size() == fast_cfg.sample_count);
  CHECK(r.total_iterations() > 0);
  CHECK(std::isfinite(r.mean()));
  CHECK(r.mean() >= 0.0);
}

TEST_CASE("nexenne::benchmark::run grows to a large iteration count for an unmeasurable body") {
  auto cfg{fast_cfg};
  cfg.sample_count = 1;
  cfg.min_iterations = 1;
  cfg.warmup = false;
  auto const r{bm::run("unmeasurable", [] noexcept {}, cfg)};
  CHECK(r.total_iterations() >= 1024);
  CHECK(std::isfinite(r.mean()));
}

TEST_CASE("nexenne::benchmark::run calibration converges to a sensible count for fast work") {
  auto cfg{bm::config{
    .target_duration = std::chrono::milliseconds{5},
    .sample_count = 4,
    .min_iterations = 1,
    .warmup = false,
  }};
  auto acc{std::uint64_t{1}};
  auto const r{bm::run(
    "fast converge",
    [&] noexcept {
      acc = acc * 6364136223846793005ull + 1442695040888963407ull;  // one LCG step
      bm::do_not_optimize(acc);
    },
    cfg
  )};
  CHECK(r.samples().size() == 4);
  CHECK(r.total_iterations() > 4u * 1024u);
  CHECK(std::isfinite(r.mean()));
  CHECK(r.mean() > 0.0);
}

TEST_CASE("nexenne::benchmark::do_not_optimize makes protected work outweigh an empty body") {
  auto cfg{bm::config{
    .target_duration = std::chrono::milliseconds{5},
    .sample_count = 5,
    .min_iterations = 1,
    .warmup = true,
  }};
  auto data{std::vector<std::uint32_t>(4096)};
  auto seed{std::uint32_t{0x9e3779b9}};
  for (auto& x : data) {
    seed = seed * 1664525u + 1013904223u;
    x = seed;
  }
  auto const empty{bm::run("empty", [] noexcept {}, cfg)};
  auto const real{bm::run(
    "sum protected",
    [&] noexcept {
      auto total{std::uint64_t{0}};
      for (auto const x : data) {
        total += x;
      }
      bm::do_not_optimize(total);
    },
    cfg
  )};
  CHECK(real.mean() > empty.mean());
}

TEST_CASE("nexenne::benchmark::result median is not noexcept because it allocates a sorted copy") {
  auto const r{bm::result{std::string{"m"}, std::vector<double>{1.0, 2.0}, 2}};
  static_assert(!noexcept(r.median()));
  static_assert(!noexcept(r.percentile(50.0)));
  CHECK(r.median() == doctest::Approx{1.5});
}

TEST_CASE("nexenne::benchmark::from_samples builds a result from externally collected samples") {
  auto const r{bm::from_samples("frames", std::vector<double>{10.0, 20.0, 30.0, 40.0}, 4)};
  CHECK(r.name() == "frames");
  CHECK(r.samples().size() == 4);
  CHECK(r.total_iterations() == 4);
  CHECK(r.mean() == doctest::Approx{25.0});
  CHECK(r.median() == doctest::Approx{25.0});
  CHECK(r.min() == doctest::Approx{10.0});
  CHECK(r.max() == doctest::Approx{40.0});
}

TEST_CASE(
  "nexenne::benchmark::result percentile interpolates and agrees with the edges and median"
) {
  auto const r{bm::from_samples("p", std::vector<double>{1.0, 2.0, 3.0, 4.0, 5.0}, 5)};
  CHECK(r.percentile(0.0) == doctest::Approx{1.0});
  CHECK(r.percentile(100.0) == doctest::Approx{5.0});
  CHECK(r.percentile(50.0) == doctest::Approx{3.0});
  CHECK(r.percentile(50.0) == doctest::Approx{r.median()});
  CHECK(r.percentile(25.0) == doctest::Approx{2.0});
  CHECK(r.percentile(10.0) == doctest::Approx{1.4});  // rank 0.4 -> 1 + 0.4 * (2 - 1)
}

TEST_CASE("nexenne::benchmark::result percentile matches median on an even sample count") {
  auto const r{
    bm::from_samples("even", std::vector<double>{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}, 8)
  };
  CHECK(r.percentile(50.0) == doctest::Approx{r.median()});
  CHECK(r.percentile(0.0) == doctest::Approx{2.0});
  CHECK(r.percentile(100.0) == doctest::Approx{9.0});
}

TEST_CASE("nexenne::benchmark::result percentile returns zero on an empty result") {
  auto const empty{bm::result{}};
  CHECK(empty.percentile(50.0) == 0.0);
  CHECK(empty.percentile(99.0) == 0.0);
}

TEST_CASE("nexenne::benchmark::config is formattable via std::format") {
  auto const cfg{bm::config{
    .target_duration = std::chrono::milliseconds{100},
    .sample_count = 10,
    .min_iterations = 1,
    .warmup = true,
  }};
  auto const s{std::format("{}", cfg)};
  CHECK(s.find("target: 100.00 ms") != std::string::npos);
  CHECK(s.find("sample_count: 10") != std::string::npos);
  CHECK(s.find("min_iterations: 1") != std::string::npos);
  CHECK(s.find("warmup: on") != std::string::npos);
}

TEST_CASE("nexenne::benchmark::config prints the same text through every layer") {
  auto const cfg{bm::config{.target_duration = std::chrono::milliseconds{5}, .sample_count = 3}};
  auto os{std::ostringstream{}};
  os << cfg;
  CHECK(bm::to_string(cfg) == std::format("{}", cfg));
  CHECK(os.str() == bm::to_string(cfg));
  CHECK(bm::to_string(cfg).find("sample_count: 3") != std::string::npos);
}

TEST_CASE("nexenne::benchmark::run honours min_iterations exactly with no warmup") {
  auto cfg{bm::config{
    .target_duration = std::chrono::nanoseconds{1},
    .sample_count = 2,
    .min_iterations = 3,
    .warmup = false,
  }};
  auto const r{
    bm::run("exact-floor", [] { std::this_thread::sleep_for(std::chrono::microseconds{20}); }, cfg)
  };
  CHECK(r.total_iterations() == 3 * 2);
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

  bm::do_not_optimize(int{123});
  bm::do_not_optimize(point{.x = 4, .y = 5.0});
}

TEST_CASE("nexenne::benchmark::clobber_memory does not disturb surrounding state") {
  auto buffer{std::vector<int>{1, 2, 3}};
  buffer[1] = 20;
  bm::clobber_memory();
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
    [&] noexcept { state = 0; },
    [&] noexcept {
      if (state != 0) {
        observed_zero_each_time = false;
      }
      ++state;
      bm::do_not_optimize(state);
    },
    cfg
  )};
  CHECK(observed_zero_each_time);
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
  CHECK(c.ratio() == doctest::Approx{0.25});
  CHECK(c.speedup() == doctest::Approx{4.0});
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
  CHECK(s.find("2.00x faster") != std::string::npos);
}

TEST_CASE(
  "nexenne::benchmark::comparison names the slower direction when the candidate regresses"
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
  CHECK(obj["mean_ns"].as_float().value() == doctest::Approx{4.0});
  CHECK(obj["median_ns"].as_float().value() == doctest::Approx{4.0});
  CHECK(obj["min_ns"].as_float().value() == doctest::Approx{2.0});
  CHECK(obj["max_ns"].as_float().value() == doctest::Approx{6.0});

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
  CHECK((*parsed)["name"].as_string().value() == name);
}

TEST_CASE("nexenne::benchmark::result is formattable via std::format") {
  auto const r{bm::result{std::string{"fmt-name"}, std::vector<double>{5.0, 7.0}, 2}};
  auto const s{std::format("{}", r)};
  CHECK(s == r.to_string());
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

TEST_CASE("nexenne::benchmark::do_not_optimize keeps a large or non-trivial mutable value") {
  auto block{std::array<int, 64>{}};
  block[5] = 7;
  bm::do_not_optimize(block);
  CHECK(block[5] == 7);

  auto text{std::string{"kept"}};
  bm::do_not_optimize(text);
  CHECK(text == "kept");
}

TEST_CASE("nexenne::benchmark::run discards a slow first call before calibrating") {
  auto calls{std::size_t{0}};
  auto const cfg{bm::config{
    .target_duration = std::chrono::milliseconds{1},
    .sample_count = 1,
    .min_iterations = 1,
    .warmup = false,
  }};
  auto const r{bm::run(
    "slow-first",
    [&calls] {
      if (calls++ == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
      bm::do_not_optimize(calls);
    },
    cfg
  )};
  CHECK(r.total_iterations() > 100);
}

TEST_CASE("nexenne::benchmark::run calibrates to a tenth of a short target on any clock") {
  using clk = nexenne::chrono::basic_manual_clock<struct calibration_tag>;
  clk::reset();
  auto calls{std::size_t{0}};
  auto const tick{[&calls] noexcept {
    ++calls;
    clk::advance(std::chrono::microseconds{1});
  }};
  auto const cfg{bm::config{
    .target_duration = std::chrono::milliseconds{1},
    .sample_count = 1,
    .min_iterations = 1,
    .warmup = false,
  }};
  auto const r{bm::run<clk>("tick", tick, cfg)};

  // 1 discarded call, batches of 1, 10, 100 (100 us, a tenth of the target), then 1000.
  CHECK(calls == 1 + 1 + 10 + 100 + 1000);
  CHECK(r.total_iterations() == 1000);
  CHECK(r.mean() == doctest::Approx(1000.0));

  calls = 0;
  clk::reset();
  auto const s{bm::run_with_setup<clk>("tick with setup", [] noexcept {}, tick, cfg)};
  CHECK(calls == 1 + 1 + 10 + 100 + 1000);
  CHECK(s.total_iterations() == 1000);
}

}  // namespace
