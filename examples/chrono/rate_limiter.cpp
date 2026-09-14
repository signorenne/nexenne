/**
 * @file
 * @brief rate_limiter token bucket over a manual clock.
 *
 * A token bucket starts full and refills lazily at a fixed rate. Here it caps a
 * burst then paces subsequent acquisitions; the manual clock makes the refill
 * timing deterministic.
 *
 * The program walks two steps:
 *
 * 1. A bucket of capacity 3 refilling at 10 tokens per second grants only 3 of a
 *    5-request initial burst.
 * 2. After 100 ms (one token at 10 per second) one acquire succeeds and an
 *    immediate second one fails.
 *
 * Expected output:
 *
 * \code
 * burst granted: 3 of 5
 * after 100 ms, acquire: true
 * immediately again: false
 * \endcode
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/rate_limiter.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct rl_example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  ch::rate_limiter<clk> limiter{3.0, 10.0};

  int granted{0};
  for (int i{0}; i < 5; ++i) {
    if (limiter.try_acquire()) {
      ++granted;
    }
  }
  std::println("burst granted: {} of 5", granted);

  clk::advance(100ms);
  std::println("after 100 ms, acquire: {}", limiter.try_acquire());
  std::println("immediately again: {}", limiter.try_acquire());
  return 0;
}
