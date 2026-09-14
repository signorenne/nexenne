/**
 * @file
 * @brief profiler: per-name aggregation of timed scopes over a manual clock.
 *
 * A profiler collects durations into named buckets and keeps count / total /
 * min / max / mean per bucket. Its natural partner is scope_timer: ask the
 * profiler for sink(name) and hand that callable to a scope_timer, and every
 * measurement under that name lands in the bucket with no map lookup per sample
 * and no allocation after the name's first use. The manual clock keeps every
 * number below exactly reproducible.
 *
 * The program walks six steps:
 *
 * 1. Cache one sink per name up front. It holds a pointer to the bucket, and map
 *    nodes are stable, so it stays valid as new names are inserted later.
 * 2. Three simulated decodes, each in a scope_timer that fires the sink when its
 *    block ends; record(name, d) is the direct path for a duration already in
 *    hand, inserting the bucket on first use.
 * 3. operator[] reads a bucket back as a copy (a zeroed stats for an unknown name,
 *    without inserting); contains and size introspect without materialising one.
 * 4. buckets() iterates every bucket, in name order since it is a std::map.
 * 5. format.hpp prints a bucket, or the whole profiler, with no helper.
 * 6. reset() zeros the stats in place but keeps the buckets, so a cached sink
 *    keeps recording into the freshly zeroed stats.
 *
 * Expected output:
 *
 * \code
 * decode: count 3, total 360.00 us, mean 120.00 us
 * decode: min 90.00 us, max 150.00 us
 * has "checksum": true, has "render": false
 * bucket count: 2
 * -- report --
 *   checksum   n=2 mean=50.00 us
 *   decode     n=3 mean=120.00 us
 * checksum: profiler_stats(count=2, total=100.00 us, min=40.00 us, max=60.00 us, mean=50.00 us)
 * after reset, decode count: 1
 * \endcode
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/format.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/profiler.hpp>
#include <nexenne/chrono/scope_timer.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct prof_example_tag>;

/**
 * @brief Formats \p d as an auto-scaled single unit (us, ms, ...).
 *
 * @param d Duration to format.
 *
 * @return The scaled, unit-suffixed text.
 *
 * @pre None.
 * @post None.
 */
auto scaled(clk::duration const d) -> std::string {
  return ch::format_scaled(std::chrono::duration_cast<std::chrono::duration<double, std::nano>>(d));
}

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  ch::profiler<clk> prof;

  auto decode_sink{prof.sink("decode")};

  for (auto const cost : {120us, 90us, 150us}) {
    ch::scope_timer<decltype(decode_sink), clk> t{decode_sink};
    clk::advance(cost);
  }

  prof.record("checksum", 40us);
  prof.record("checksum", 60us);

  auto const d{prof["decode"]};
  std::println("decode: count {}, total {}, mean {}", d.count, scaled(d.total), scaled(d.mean()));
  std::println("decode: min {}, max {}", scaled(d.min), scaled(d.max));

  std::println(
    "has \"checksum\": {}, has \"render\": {}", prof.contains("checksum"), prof.contains("render")
  );
  std::println("bucket count: {}", prof.size());

  std::println("-- report --");
  for (auto const& [name, s] : prof.buckets()) {
    std::println("  {:<10} n={} mean={}", name, s.count, scaled(s.mean()));
  }

  std::println("checksum: {}", prof["checksum"]);

  prof.reset();
  {
    ch::scope_timer<decltype(decode_sink), clk> t{decode_sink};
    clk::advance(75us);
  }
  std::println("after reset, decode count: {}", prof["decode"].count);

  return 0;
}
