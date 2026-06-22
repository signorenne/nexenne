/**
 * @file
 * @brief deadline: an absolute "must be done by" instant with reached/remaining.
 *
 * A deadline wraps an absolute time_point and answers questions about it
 * relative to the live clock: reached() (are we past it?) and remaining() (how
 * long is left, clamped at zero so the overdue case needs no guard). It is the
 * natural companion to a retry loop or a cancellable wait - arm it once, then
 * poll cheaply. The manual clock makes the timing deterministic.
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

  // after(d) anchors the deadline at now + d in one clock read. (at(tp) is the
  // sibling factory when you already hold an absolute target.)
  auto const dl{ch::deadline<clk>::after(100ms)};

  // A retry loop polling the deadline: attempt work, and stop either on success
  // or once the deadline is reached. remaining() never goes negative, so the
  // loop condition stays simple.
  int attempts{0};
  bool succeeded{false};
  while (!dl.reached()) {
    ++attempts;
    std::println(
      "attempt {}: {} ms left", attempts, dl.remaining<std::chrono::milliseconds>().count()
    );
    if (attempts == 3) {  // pretend the 3rd attempt succeeds
      succeeded = true;
      break;
    }
    clk::advance(30ms);  // each attempt costs ~30 ms
  }
  std::println("succeeded {} after {} attempts", succeeded, attempts);

  // Now let one expire to show the overdue path: reached() flips true and
  // remaining() clamps to zero rather than reporting a negative duration.
  auto const tight{ch::deadline<clk>::after(10ms)};
  clk::advance(25ms);  // blow past it
  std::println(
    "expired: reached {}, remaining {} ms",
    tight.reached(),
    tight.remaining<std::chrono::milliseconds>().count()
  );

  // Deadlines are ordered by absolute target time, so they sort directly. The
  // earlier-firing deadline compares less.
  auto const soon{ch::deadline<clk>::after(5ms)};
  auto const later{ch::deadline<clk>::after(500ms)};
  std::println("soon < later: {}", soon < later);

  // attempt 1: 100 ms left
  // attempt 2: 70 ms left
  // attempt 3: 40 ms left
  // succeeded true after 3 attempts
  // expired: reached true, remaining 0 ms
  // soon < later: true
  return 0;
}
