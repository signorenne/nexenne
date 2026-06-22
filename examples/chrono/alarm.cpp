/**
 * @file
 * @brief alarm: a polling timer that bakes the "what to do" into itself.
 *
 * Where countdown and deadline leave the polling caller to decide what happens
 * on expiry, an alarm stores a callback and fires it for you. poll(now) checks
 * the fire time against the supplied instant and invokes the callback - once for
 * a one-shot, every elapsed cycle for a periodic alarm (it catches up if you
 * poll late). The callback lives in an in_place_function with inline storage, so
 * the alarm never allocates. The manual clock keeps the timing deterministic.
 *
 * Note: poll() takes an explicit `now` time_point rather than reading the clock
 * itself - that is what makes the catch-up behaviour testable and lets you drive
 * many alarms from one shared now() snapshot.
 */

#include <print>

#include <nexenne/chrono/alarm.hpp>
#include <nexenne/chrono/manual_clock.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct alarm_example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();

  // --- one-shot ---
  // Fire exactly once, 50 ms from now, then disarm.
  int oneshot_fires{0};
  ch::alarm<clk> once;
  once.set_callback([&oneshot_fires] { ++oneshot_fires; });
  once.arm_after(clk::now(), 50ms);

  clk::advance(30ms);
  once.poll(clk::now());  // not yet: still armed, no fire
  std::println("at 30ms: armed {}, fires {}", once.is_armed(), oneshot_fires);

  clk::advance(40ms);     // total 70ms, past the 50ms deadline
  once.poll(clk::now());  // fires once, then disarms
  std::println("at 70ms: armed {}, fires {}", once.is_armed(), oneshot_fires);

  // --- periodic with catch-up ---
  // Fire every 100 ms. We will poll late, after several boundaries have passed,
  // and watch poll() catch up by firing once per missed cycle.
  clk::reset();
  int tick_fires{0};
  ch::alarm<clk> blink;
  blink.set_callback([&tick_fires] { ++tick_fires; });
  blink.arm_periodic(clk::now(), 100ms);

  clk::advance(350ms);     // three boundaries elapsed: 100, 200, 300
  blink.poll(clk::now());  // catches up, firing three times in this one poll
  std::println(
    "periodic after 350ms: fires {}, next at {} ms",
    tick_fires,
    std::chrono::duration_cast<std::chrono::milliseconds>(blink.next_fire_time().time_since_epoch())
      .count()
  );

  clk::advance(100ms);     // one more boundary at 400
  blink.poll(clk::now());  // one more fire
  std::println("periodic after 450ms: fires {}", tick_fires);

  // disarm() stops further firing but keeps the callback for re-arming later.
  blink.disarm();
  clk::advance(500ms);
  blink.poll(clk::now());
  std::println("after disarm: armed {}, fires {}", blink.is_armed(), tick_fires);

  // at 30ms: armed true, fires 0
  // at 70ms: armed false, fires 1
  // periodic after 350ms: fires 3, next at 400 ms
  // periodic after 450ms: fires 4
  // after disarm: armed false, fires 4
  return 0;
}
