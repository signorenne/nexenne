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
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/profiler.hpp>
#include <nexenne/chrono/scope_timer.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct prof_example_tag>;

auto scaled(clk::duration const d) -> std::string {
  return ch::format_scaled(std::chrono::duration_cast<std::chrono::duration<double, std::nano>>(d));
}

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  ch::profiler<clk> prof;

  // Cache one sink per name up front. The sink is a small callable holding a
  // pointer to the bucket; map iterators are stable, so it stays valid even as
  // new names are inserted later.
  auto decode_sink{prof.sink("decode")};

  // Simulate decoding three messages with varying cost. Each scope_timer fires
  // its sink when the block ends, folding the elapsed time into "decode".
  for (auto const cost : {120us, 90us, 150us}) {
    ch::scope_timer<decltype(decode_sink), clk> t{decode_sink};
    clk::advance(cost);
  }

  // record(name, d) is the direct path when you already have a duration in hand
  // and do not want a scope_timer. It inserts the bucket on first use of a name.
  prof.record("checksum", 40us);
  prof.record("checksum", 60us);

  // Read one bucket back. operator[] returns a copy of the stats, or a zeroed
  // stats for an unknown name (it does not insert).
  auto const d{prof["decode"]};
  std::println("decode: count {}, total {}, mean {}", d.count, scaled(d.total), scaled(d.mean()));
  std::println("decode: min {}, max {}", scaled(d.min), scaled(d.max));

  // contains / size let you introspect without materialising a bucket.
  std::println(
    "has \"checksum\": {}, has \"render\": {}", prof.contains("checksum"), prof.contains("render")
  );
  std::println("bucket count: {}", prof.size());

  // Iterate every bucket; the underlying std::map yields names in sorted order.
  std::println("-- report --");
  for (auto const& [name, s] : prof.buckets()) {
    std::println("  {:<10} n={} mean={}", name, s.count, scaled(s.mean()));
  }

  // reset() zeros stats in place but keeps the buckets, so cached sinks stay
  // valid and keep recording into the freshly-zeroed stats.
  prof.reset();
  {
    ch::scope_timer<decltype(decode_sink), clk> t{decode_sink};
    clk::advance(75us);  // still routes into "decode" after the reset
  }
  std::println("after reset, decode count: {}", prof["decode"].count);

  // decode: count 3, total 360.00 us, mean 120.00 us
  // decode: min 90.00 us, max 150.00 us
  // has "checksum": true, has "render": false
  // bucket count: 2
  // -- report --
  //   checksum   n=2 mean=50.00 us
  //   decode     n=3 mean=120.00 us
  // after reset, decode count: 1
  return 0;
}
