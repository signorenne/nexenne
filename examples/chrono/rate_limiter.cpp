/**
 * @file
 * @brief rate_limiter token bucket over a manual clock.
 *
 * A token bucket starts full and refills lazily at a fixed rate. Here it caps a
 * burst then paces subsequent acquisitions; the manual clock makes the refill
 * timing deterministic.
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
  ch::rate_limiter<clk> limiter{3.0, 10.0};  // capacity 3, 10 tokens/sec

  int granted{0};
  for (int i{0}; i < 5; ++i) {  // initial burst: only 3 fit
    if (limiter.try_acquire()) {
      ++granted;
    }
  }
  std::println("burst granted: {} of 5", granted);

  clk::advance(100ms);  // 10/sec means one token per 100 ms
  std::println("after 100 ms, acquire: {}", limiter.try_acquire());
  std::println("immediately again: {}", limiter.try_acquire());
  // burst granted: 3 of 5
  // after 100 ms, acquire: true
  // immediately again: false
  return 0;
}
