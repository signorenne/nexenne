/**
 * @file
 * @brief deadline: an absolute "must be done by" instant with reached/remaining.
 *
 * A deadline wraps an absolute time_point and answers questions about it
 * relative to the live clock: reached() (are we past it?) and remaining() (how
 * long is left, clamped at zero so the overdue case needs no guard). It is the
 * natural companion to a retry loop or a cancellable wait: arm it once, then
 * poll cheaply. The manual clock makes the timing deterministic.
 *
 * The program walks three steps:
 *
 * 1. after(d) anchors a 100 ms deadline at now + d in one clock read (at(tp) is
 *    the sibling factory for an absolute target). A retry loop, each attempt
 *    costing 30 ms, polls it and stops on success (the third attempt) or once it
 *    is reached; remaining() never goes negative, so the loop stays simple.
 * 2. A 10 ms deadline is overrun by 25 ms: reached() flips true and remaining()
 *    clamps to zero rather than reporting a negative duration.
 * 3. Deadlines order by absolute target time, so the earlier one compares less
 *    and they sort directly.
 *
 * Expected output:
 *
 * \code
 * attempt 1: 100 ms left
 * attempt 2: 70 ms left
 * attempt 3: 40 ms left
 * succeeded true after 3 attempts
 * expired: reached true, remaining 0 ms
 * soon < later: true
 * \endcode
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/deadline.hpp>
#include <nexenne/chrono/manual_clock.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct deadline_example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();

  auto const dl{ch::deadline<clk>::after(100ms)};

  int attempts{0};
  bool succeeded{false};
  while (!dl.reached()) {
    ++attempts;
    std::println(
      "attempt {}: {} ms left", attempts, dl.remaining<std::chrono::milliseconds>().count()
    );
    if (attempts == 3) {
      succeeded = true;
      break;
    }
    clk::advance(30ms);
  }
  std::println("succeeded {} after {} attempts", succeeded, attempts);

  auto const tight{ch::deadline<clk>::after(10ms)};
  clk::advance(25ms);
  std::println(
    "expired: reached {}, remaining {} ms",
    tight.reached(),
    tight.remaining<std::chrono::milliseconds>().count()
  );

  auto const soon{ch::deadline<clk>::after(5ms)};
  auto const later{ch::deadline<clk>::after(500ms)};
  std::println("soon < later: {}", soon < later);

  return 0;
}
