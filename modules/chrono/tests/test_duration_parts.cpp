/**
 * @file
 * @brief Tests for nexenne::chrono duration breakdown and formatting.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <format>
#include <limits>
#include <string>

#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/format.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::extract_parts splits a duration into components") {
  auto const p{ch::extract_parts(std::chrono::milliseconds{90'061'500})};
  CHECK(p.sign == 1);
  CHECK(p.days == 1);
  CHECK(p.hours == 1);
  CHECK(p.minutes == 1);
  CHECK(p.seconds == 1);
  CHECK(p.millis == 500);
}

TEST_CASE("nexenne::chrono::extract_parts carries the sign, magnitudes stay non-negative") {
  auto const p{ch::extract_parts(-1500ms)};
  CHECK(p.sign == -1);
  CHECK(p.seconds == 1);
  CHECK(p.millis == 500);

  auto const z{ch::extract_parts(0ms)};
  CHECK(z.sign == 0);
}

TEST_CASE("nexenne::chrono::extract_parts rounds to seconds, ties away from zero") {
  auto const up{ch::extract_parts(1500ms, true)};
  CHECK(up.seconds == 2);
  CHECK(up.millis == 0);
  auto const down{ch::extract_parts(1400ms, true)};
  CHECK(down.seconds == 1);
}

TEST_CASE("nexenne::chrono::format renders with the default token layout") {
  auto const s{ch::format(std::chrono::milliseconds{90'061'500})};
  CHECK(s == "01d:01h:01m:01s:500ms");
}

TEST_CASE("nexenne::chrono::format suppresses leading zero components") {
  CHECK(ch::format(65s) == "01m:05s");
  CHECK(ch::format(5s) == "05s");
}

TEST_CASE("nexenne::chrono::format without {ms} rounds to whole seconds") {
  CHECK(ch::format(1500ms, "{s}s") == "02s");
}

TEST_CASE("nexenne::chrono::extract_parts saturates a too-coarse, too-large duration") {
  auto const p{ch::extract_parts(std::chrono::hours::max())};
  CHECK(p.sign == 1);
  CHECK(p.days > 0);
}

TEST_CASE("nexenne::chrono::extract_parts rounds milliseconds::max() without overflow") {
  auto const p{ch::extract_parts(std::chrono::milliseconds::max(), true)};
  CHECK(p.sign == 1);
  CHECK(p.millis == 0);
  auto const s{ch::format(std::chrono::milliseconds::max(), "{s}s")};
  CHECK_FALSE(s.empty());
}

TEST_CASE("nexenne::chrono::format_scaled auto-scales to a single SI unit") {
  using ns_d = std::chrono::duration<double, std::nano>;
  CHECK(ch::format_scaled(ns_d{0.0}) == "0.00 ns");
  CHECK(ch::format_scaled(ns_d{999.0}) == "999.00 ns");
  CHECK(ch::format_scaled(ns_d{4170.0}) == "4.17 us");
  CHECK(ch::format_scaled(ns_d{1000.0}) == "1.00 us");
  CHECK(ch::format_scaled(std::chrono::milliseconds{5}) == "5.00 ms");
  CHECK(ch::format_scaled(std::chrono::seconds{2}) == "2.00 s");
  CHECK(ch::format_scaled(ns_d{-4170.0}) == "-4.17 us");
  CHECK(ch::format_scaled(ns_d{4170.0}, 1) == "4.2 us");
  CHECK(ch::format_scaled(ns_d{-0.0}) == "0.00 ns");
}

TEST_CASE("nexenne::chrono duration_parts std::formatter") {
  auto const p{ch::extract_parts(65s)};
  CHECK(std::format("{}", p) == "01m:05s");
  CHECK(std::format("{:!}", p) == "00d:00h:01m:05s.000");
}

TEST_CASE("nexenne::chrono::extract_parts is usable in a constant expression") {
  constexpr auto p{ch::extract_parts(std::chrono::milliseconds{90'061'500})};
  static_assert(p.sign == 1);
  static_assert(p.days == 1);
  static_assert(p.hours == 1);
  static_assert(p.minutes == 1);
  static_assert(p.seconds == 1);
  static_assert(p.millis == 500);
  constexpr auto z{ch::extract_parts(0ms)};
  static_assert(z.sign == 0 && z.days == 0 && z.millis == 0);
  CHECK(true);
}

TEST_CASE("nexenne::chrono::extract_parts zero has zero sign and all-zero magnitudes") {
  auto const z{ch::extract_parts(0ms)};
  CHECK(z.sign == 0);
  CHECK(z.days == 0);
  CHECK(z.hours == 0);
  CHECK(z.minutes == 0);
  CHECK(z.seconds == 0);
  CHECK(z.millis == 0);
  auto const zg{ch::extract_parts(std::chrono::hours{0})};
  CHECK(zg.sign == 0);
  CHECK(zg.days == 0);
}

TEST_CASE("nexenne::chrono::extract_parts isolates exactly one of each unit") {
  auto const d{ch::extract_parts(std::chrono::milliseconds{86'400'000})};
  CHECK(d.days == 1);
  CHECK(d.hours == 0);
  CHECK(d.minutes == 0);
  CHECK(d.seconds == 0);
  CHECK(d.millis == 0);

  auto const h{ch::extract_parts(std::chrono::hours{1})};
  CHECK(h.days == 0);
  CHECK(h.hours == 1);

  auto const m{ch::extract_parts(std::chrono::minutes{1})};
  CHECK(m.minutes == 1);
  CHECK(m.hours == 0);

  auto const s{ch::extract_parts(std::chrono::seconds{1})};
  CHECK(s.seconds == 1);
  CHECK(s.millis == 0);

  auto const ms{ch::extract_parts(std::chrono::milliseconds{1})};
  CHECK(ms.millis == 1);
  CHECK(ms.seconds == 0);
}

TEST_CASE("nexenne::chrono::extract_parts carries at the 59.999s boundary") {
  auto const just_under{ch::extract_parts(59'999ms)};
  CHECK(just_under.minutes == 0);
  CHECK(just_under.seconds == 59);
  CHECK(just_under.millis == 999);
  auto const carry{ch::extract_parts(60'000ms)};
  CHECK(carry.minutes == 1);
  CHECK(carry.seconds == 0);
  CHECK(carry.millis == 0);
  auto const day_edge{ch::extract_parts(std::chrono::milliseconds{86'399'999})};
  CHECK(day_edge.days == 0);
  CHECK(day_edge.hours == 23);
  CHECK(day_edge.minutes == 59);
  CHECK(day_edge.seconds == 59);
  CHECK(day_edge.millis == 999);
  auto const next_day{ch::extract_parts(std::chrono::milliseconds{86'400'000})};
  CHECK(next_day.days == 1);
  CHECK(next_day.hours == 0);
}

TEST_CASE("nexenne::chrono::extract_parts mirrors the sign onto every part") {
  auto const neg{ch::extract_parts(std::chrono::milliseconds{-90'061'500})};
  CHECK(neg.sign == -1);
  CHECK(neg.days == 1);
  CHECK(neg.hours == 1);
  CHECK(neg.minutes == 1);
  CHECK(neg.seconds == 1);
  CHECK(neg.millis == 500);

  auto const pos{ch::extract_parts(std::chrono::milliseconds{90'061'500})};
  CHECK(neg.days == pos.days);
  CHECK(neg.hours == pos.hours);
  CHECK(neg.minutes == pos.minutes);
  CHECK(neg.seconds == pos.seconds);
  CHECK(neg.millis == pos.millis);

  auto const tiny{ch::extract_parts(-1ms)};
  CHECK(tiny.sign == -1);
  CHECK(tiny.millis == 1);
}

TEST_CASE("nexenne::chrono::extract_parts rounds negative values, ties away from zero") {
  auto const r{ch::extract_parts(-1500ms, true)};
  CHECK(r.sign == -1);
  CHECK(r.seconds == 2);
  CHECK(r.millis == 0);
  auto const r2{ch::extract_parts(-1400ms, true)};
  CHECK(r2.sign == -1);
  CHECK(r2.seconds == 1);
  auto const r3{ch::extract_parts(-500ms, true)};
  CHECK(r3.sign == -1);
  CHECK(r3.seconds == 1);
  CHECK(r3.millis == 0);
  auto const r4{ch::extract_parts(500ms, true)};
  CHECK(r4.sign == 1);
  CHECK(r4.seconds == 1);
  auto const r5{ch::extract_parts(499ms, true)};
  CHECK(r5.sign == 0);
  CHECK(r5.seconds == 0);
  CHECK(r5.millis == 0);
}

TEST_CASE("nexenne::chrono::extract_parts saturates the most-negative coarse duration") {
  auto const p{ch::extract_parts(std::chrono::hours::min())};
  CHECK(p.sign == -1);
  CHECK(p.days > 0);
}

TEST_CASE("nexenne::chrono::extract_parts handles both millisecond extremes without UB") {
  auto const hi{ch::extract_parts(std::chrono::milliseconds::max())};
  CHECK(hi.sign == 1);
  CHECK(hi.days > 0);
  auto const lo{ch::extract_parts(std::chrono::milliseconds::min())};
  CHECK(lo.sign == -1);
  CHECK(lo.days > 0);
  auto const lo_round{ch::extract_parts(std::chrono::milliseconds::min(), true)};
  CHECK(lo_round.sign == -1);
  CHECK(lo_round.millis == 0);
}

TEST_CASE("nexenne::chrono::extract_parts clamps a coarse positive too large for milliseconds") {
  auto const big{ch::extract_parts(std::chrono::days::max())};
  auto const cap{ch::extract_parts(std::chrono::milliseconds::max())};
  CHECK(big.sign == 1);
  CHECK(big.days == cap.days);
  CHECK(big.days > 0);
}

TEST_CASE("nexenne::chrono::extract_parts reassembles into the original duration") {
  using std::chrono::days;
  using std::chrono::hours;
  using std::chrono::milliseconds;
  using std::chrono::minutes;
  using std::chrono::seconds;
  auto const src{milliseconds{90'061'500}};
  auto const p{ch::extract_parts(src)};
  auto total{
    days{p.days} + hours{p.hours} + minutes{p.minutes} + seconds{p.seconds} + milliseconds{p.millis}
  };
  if (p.sign < 0) {
    total = -total;
  }
  CHECK(total == src);

  auto const nsrc{milliseconds{-90'061'500}};
  auto const np{ch::extract_parts(nsrc)};
  auto ntotal{
    days{np.days} + hours{np.hours} + minutes{np.minutes} + seconds{np.seconds}
    + milliseconds{np.millis}
  };
  if (np.sign < 0) {
    ntotal = -ntotal;
  }
  CHECK(ntotal == nsrc);
}

TEST_CASE("nexenne::chrono::format emits the explicit sign tokens") {
  CHECK(ch::format(5s, "{s+}{s}s") == "+05s");
  CHECK(ch::format(-5s, "{s-}{s}s") == "-05s");
  CHECK(ch::format(5s, "{s-}{s}s") == "05s");
  CHECK(ch::format(-5s, "{s+}{s}s") == "05s");
  CHECK(ch::format(-5s, "{s-}{s}s", true, "+", "neg ") == "neg 05s");
}

TEST_CASE("nexenne::chrono::format without suppress_zero shows every component") {
  CHECK(ch::format(65s, "{d}d:{h}h:{m}m:{s}s.{ms}", false) == "00d:00h:01m:05s.000");
  CHECK(ch::format(0ms, "{d}d:{h}h:{m}m:{s}s.{ms}", false) == "00d:00h:00m:00s.000");
}

TEST_CASE("nexenne::chrono::format with suppress_zero keeps seconds even when zero magnitude") {
  CHECK(ch::format(0ms) == "00s");
  CHECK(ch::format(500ms) == "00s:500ms");
  CHECK(ch::format(std::chrono::hours{1}) == "01h:00m:00s");
}

TEST_CASE("nexenne::chrono::format keeps interior zero components") {
  auto const d{
    ch::format(std::chrono::hours{24} + std::chrono::minutes{5} + std::chrono::seconds{34})
  };
  CHECK(d == "01d:00h:05m:34s");
  CHECK(ch::format(std::chrono::minutes{5} + std::chrono::seconds{34}) == "05m:34s");
}

TEST_CASE("nexenne::chrono::format honors the caller token layout under suppress_zero") {
  CHECK(ch::format(std::chrono::milliseconds{3'750'000}, "{h}h:{m}m") == "01h:02m");
  CHECK(ch::format(std::chrono::milliseconds{3'750'000}, "{h}h:{m}m", false) == "01h:02m");
  CHECK(ch::format(std::chrono::seconds{90}, "{m}m:{s}s") == "01m:30s");
}

TEST_CASE("nexenne::chrono::format rounds when {ms} is absent and keeps it when present") {
  CHECK(ch::format(1500ms, "{s}s") == "02s");
  CHECK(ch::format(1500ms, "{s}s.{ms}") == "01s:500ms");
  CHECK(ch::format(1500ms, "{s}s.{ms}", false) == "01s.500");
  CHECK(ch::format(1400ms, "{s}s") == "01s");
}

TEST_CASE("nexenne::chrono::format generic overload saturates a too-coarse input") {
  auto const s{ch::format(std::chrono::hours::max(), "{d}d")};
  CHECK_FALSE(s.empty());
  auto const sn{ch::format(std::chrono::hours::min(), "{s-}{d}d")};
  CHECK_FALSE(sn.empty());
  CHECK(sn.front() == '-');
}

TEST_CASE("nexenne::chrono::format_scaled covers each unit boundary and negatives") {
  using ns_d = std::chrono::duration<double, std::nano>;
  CHECK(ch::format_scaled(ns_d{999'999.0}) == "1000.00 us");
  CHECK(ch::format_scaled(ns_d{1'000'000.0}) == "1.00 ms");
  CHECK(ch::format_scaled(ns_d{999'999'999.0}) == "1000.00 ms");
  CHECK(ch::format_scaled(ns_d{1'000'000'000.0}) == "1.00 s");
  CHECK(ch::format_scaled(ns_d{5'000'000'000.0}) == "5.00 s");
  CHECK(ch::format_scaled(ns_d{-500.0}) == "-500.00 ns");
  CHECK(ch::format_scaled(std::chrono::seconds{2}, 0) == "2 s");
  CHECK(ch::format_scaled(std::chrono::nanoseconds{4170}) == "4.17 us");
}

TEST_CASE("nexenne::chrono::format_scaled preserves a fractional floating sub-unit") {
  using ns_d = std::chrono::duration<double, std::nano>;
  CHECK(ch::format_scaled(ns_d{1.5}) == "1.50 ns");
  CHECK(ch::format_scaled(ns_d{0.25}, 2) == "0.25 ns");
  CHECK(ch::format_scaled(ns_d{-0.0}, 3) == "0.000 ns");
}

TEST_CASE("nexenne::chrono duration_parts std::formatter round-trips suppress flag") {
  auto const full{ch::extract_parts(std::chrono::milliseconds{90'061'500})};
  CHECK(std::format("{}", full) == "01d:01h:01m:01s:500ms");
  CHECK(std::format("{:!}", full) == "01d:01h:01m:01s.500");
  auto const neg{ch::extract_parts(-65s)};
  CHECK(std::format("{}", neg) == "-01m:05s");
  auto const z{ch::extract_parts(0ms)};
  CHECK(std::format("{}", z) == "00s");
  CHECK(std::format("{:!}", z) == "00d:00h:00m:00s.000");
}

TEST_CASE("nexenne::chrono::duration_parts formatter handles the milliseconds minimum") {
  auto const low{std::chrono::milliseconds::min()};
  auto const parts{ch::extract_parts(low)};
  CHECK(std::format("{}", parts) == ch::format(low));
  CHECK(
    std::format("{}", ch::extract_parts(std::chrono::milliseconds{-90'061'500}))
    == ch::format(std::chrono::milliseconds{-90'061'500})
  );
}

}  // namespace
