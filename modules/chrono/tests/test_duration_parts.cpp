/**
 * @file
 * @brief Tests for nexenne::chrono duration breakdown and formatting.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <format>
#include <string>

#include <nexenne/chrono/duration_parts.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::extract_parts splits a duration into components") {
  auto const p{ch::extract_parts(std::chrono::milliseconds{90'061'500})};
  // 1 day, 1 hour, 1 minute, 1 second, 500 ms
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
  CHECK(up.seconds == 2);  // 1.5 s rounds to 2
  CHECK(up.millis == 0);
  auto const down{ch::extract_parts(1400ms, true)};
  CHECK(down.seconds == 1);
}

TEST_CASE("nexenne::chrono::format renders with the default token layout") {
  // In suppress-zero mode (the default) every surviving component is joined
  // with ':', so the millisecond piece is ':500ms', not '.500ms'.
  auto const s{ch::format(std::chrono::milliseconds{90'061'500})};
  CHECK(s == "01d:01h:01m:01s:500ms");
}

TEST_CASE("nexenne::chrono::format suppresses leading zero components") {
  // 1 minute 5 seconds: days and hours dropped, joined with ':'
  CHECK(ch::format(65s) == "01m:05s");
  CHECK(ch::format(5s) == "05s");
}

TEST_CASE("nexenne::chrono::format without {ms} rounds to whole seconds") {
  CHECK(ch::format(1500ms, "{s}s") == "02s");  // rounds 1.5 -> 2
}

TEST_CASE("nexenne::chrono::extract_parts saturates a too-coarse, too-large duration") {
  // hours::max() in milliseconds would overflow int64; the generic overload
  // clamps to the representable range instead of invoking UB.
  auto const p{ch::extract_parts(std::chrono::hours::max())};
  CHECK(p.sign == 1);  // defined, positive result (no overflow/UB)
  CHECK(p.days > 0);
}

TEST_CASE("nexenne::chrono::extract_parts rounds milliseconds::max() without overflow") {
  // Rounding milliseconds::max() up to a whole second must not multiply back out
  // of int64 range (UBSan would flag a signed overflow). It saturates instead.
  auto const p{ch::extract_parts(std::chrono::milliseconds::max(), true)};
  CHECK(p.sign == 1);  // defined, positive
  CHECK(p.millis == 0);
  // format without {ms} also forces the round-to-seconds path.
  auto const s{ch::format(std::chrono::milliseconds::max(), "{s}s")};
  CHECK_FALSE(s.empty());
}

TEST_CASE("nexenne::chrono::format_scaled auto-scales to a single SI unit") {
  using ns_d = std::chrono::duration<double, std::nano>;
  CHECK(ch::format_scaled(ns_d{0.0}) == "0.00 ns");
  CHECK(ch::format_scaled(ns_d{999.0}) == "999.00 ns");
  CHECK(ch::format_scaled(ns_d{4170.0}) == "4.17 us");  // sub-ms resolution
  CHECK(ch::format_scaled(ns_d{1000.0}) == "1.00 us");  // exact boundary
  CHECK(ch::format_scaled(std::chrono::milliseconds{5}) == "5.00 ms");
  CHECK(ch::format_scaled(std::chrono::seconds{2}) == "2.00 s");
  CHECK(ch::format_scaled(ns_d{-4170.0}) == "-4.17 us");  // sign preserved
  CHECK(ch::format_scaled(ns_d{4170.0}, 1) == "4.2 us");  // precision honoured
  CHECK(ch::format_scaled(ns_d{-0.0}) == "0.00 ns");      // negative zero normalised
}

TEST_CASE("nexenne::chrono duration_parts std::formatter") {
  auto const p{ch::extract_parts(65s)};
  CHECK(std::format("{}", p) == "01m:05s");
  // '!' disables suppress-zero, using the literal default layout (with .000 ms)
  CHECK(std::format("{:!}", p) == "00d:00h:01m:05s.000");
}

}  // namespace
