/**
 * @file
 * @brief Human-readable duration breakdown and Hz/period conversions.
 *
 * The helper layer of nexenne::chrono: split a duration into d/h/m/s/ms parts,
 * render it with format, and convert between a frequency and its period, all at
 * compile time where possible. No clock is involved.
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/conversion.hpp>
#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/frequency.hpp>

namespace {

namespace ch = nexenne::chrono;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  std::println("90061500 ms = {}", ch::format(std::chrono::milliseconds{90'061'500}));
  std::println("65 s = {}", ch::format(65s));

  // 1 kHz period, and the inverse.
  constexpr auto period{ch::period_from<ch::hertz<1000>>()};
  std::println(
    "1 kHz period = {} us", std::chrono::duration_cast<std::chrono::microseconds>(period).count()
  );
  std::println("period 1 ms = {} Hz", ch::hertz_from(1ms));

  // Saturating conversion for a fixed-width embedded API field.
  std::println("5000 s as u32 us (saturates) = {}", ch::to_us_u32(5000s));
  // 90061500 ms = 01d:01h:01m:01s:500ms
  // 65 s = 01m:05s
  // 1 kHz period = 1000 us
  // period 1 ms = 1000 Hz
  // 5000 s as u32 us (saturates) = 4294967295
  return 0;
}
