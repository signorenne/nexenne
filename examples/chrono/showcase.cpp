/**
 * @file
 * @brief A guided tour of nexenne::chrono through one realistic task: the timing
 *        spine of a game / render loop with a built-in profiler.
 *
 * This program does not draw anything: it *runs the clock* a real engine would,
 * and prints the timings, so you can see how the module's pieces fit together in
 * context:
 *
 *   1. Drive the loop      -> frame_timer for the per-frame delta and FPS. tick()
 *                             folds each delta into a moving window (here four
 *                             frames), so fps() is a smoothed recent average, not
 *                             one jittery sample. The first tick() has no previous
 *                             frame, so it returns zero: never divide by it.
 *   2. Cap the frame rate  -> rate_limiter as a "may I render now?" gate. One token
 *                             is one frame; capacity 1 refilling at 200 per second
 *                             allows a frame every 5 ms. until_next_token() says
 *                             how long to wait; the manual clock advances by that
 *                             much where a live loop would sleep_for(wait).
 *   3. Profile the phases  -> scope_timer feeding per-name buckets in a profiler.
 *                             A sink(name) caches its bucket pointer, so recording
 *                             needs no map lookup and no allocation. Each timer
 *                             fires when its own block ends, and names the manual
 *                             clock explicitly since it defaults to steady_clock.
 *   4. Budget one frame    -> stopwatch + deadline to catch a frame that overran
 *                             the 120 fps budget of 8.33 ms. The deadline is armed
 *                             once at frame start and asked reached() at the end;
 *                             the planned spike in frame 4 (a GC pause, an asset
 *                             load) is the one that blows it.
 *   5. Run a timed phase   -> countdown for a fixed 10 ms "intro" segment; tick()
 *                             is true on the single frame that crosses it.
 *   6. Report              -> format_scaled keeps the sub-millisecond resolution
 *                             the per-phase table needs; format's d/h/m/s breakdown
 *                             suits the human-read total run time.
 *
 * Every nexenne::chrono type is templated on its clock. A shipping engine would
 * use the default std::chrono::steady_clock and let real wall time pass; here we
 * drive a manual_clock by hand so every number below is exactly reproducible -
 * no sleeps, no flakiness, no dependence on how fast the machine is. Swapping
 * clk for std::chrono::steady_clock is the only change needed to make this a
 * live loop.
 */

#include <array>
#include <chrono>
#include <cstdint>
#include <print>

#include <nexenne/chrono/countdown.hpp>
#include <nexenne/chrono/deadline.hpp>
#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/frame_timer.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/profiler.hpp>
#include <nexenne/chrono/rate_limiter.hpp>
#include <nexenne/chrono/scope_timer.hpp>
#include <nexenne/chrono/stopwatch.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

/// @brief The simulation's hand-advanced clock; its own tag keeps its state separate.
using clk = ch::basic_manual_clock<struct showcase_tag>;

/**
 * @brief Simulates a phase's work by advancing the manual clock by \p cost.
 *
 * Real CPU work would time non-deterministically; a fixed advance lets the
 * scope_timer measure exactly that cost.
 *
 * @param cost Simulated cost of the phase.
 *
 * @pre None.
 * @post \c clk::now() has advanced by \p cost.
 */
auto burn(clk::duration const cost) noexcept -> void {
  clk::advance(cost);
}

/**
 * @brief Formats \p d as an auto-scaled single unit (us, ms, ...).
 *
 * Converts to a double-based nanosecond duration first so format_scaled keeps
 * fractional precision; an integer millisecond duration would round 4170 us down
 * to "4 ms".
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
  clk::reset();

  struct frame_plan {
    clk::duration update;
    clk::duration physics;
    clk::duration render;
  };

  constexpr std::array<frame_plan, 6> plan{{
    {1200us, 800us, 3000us},
    {1100us, 820us, 3200us},
    {1300us, 760us, 3100us},
    {1250us, 900us, 9000us},
    {1180us, 810us, 2950us},
    {1220us, 780us, 3050us},
  }};

  std::println("== 1. Frame loop ==");
  ch::frame_timer<4, clk> frames;

  ch::rate_limiter<clk> gate{1.0, 200.0};

  ch::profiler<clk> prof;
  auto update_sink{prof.sink("update")};
  auto physics_sink{prof.sink("physics")};
  auto render_sink{prof.sink("render")};

  constexpr auto frame_budget{8333us};
  std::uint64_t blown_budgets{0};

  ch::countdown<clk> intro{10ms};
  intro.start();
  bool intro_done{false};

  for (std::size_t i{0}; i < plan.size(); ++i) {
    auto const wait{gate.until_next_token()};
    if (wait > clk::duration::zero()) {
      clk::advance(wait);
    }
    nexenne::utility::ignore(gate.try_acquire());

    auto const dt{frames.tick()};

    auto const budget{ch::deadline<clk>::after(frame_budget)};
    ch::stopwatch<clk> frame_sw;
    frame_sw.start();

    auto const& f{plan[i]};

    {
      ch::scope_timer<decltype(update_sink), clk> t{update_sink};
      burn(f.update);
    }
    {
      ch::scope_timer<decltype(physics_sink), clk> t{physics_sink};
      burn(f.physics);
    }
    {
      ch::scope_timer<decltype(render_sink), clk> t{render_sink};
      burn(f.render);
    }

    if (intro.tick()) {
      intro_done = true;
      std::println("  frame {}: intro phase complete, gameplay begins", i + 1);
    }

    auto const cpu{frame_sw.elapsed()};
    auto const over_budget{budget.reached()};
    if (over_budget) {
      ++blown_budgets;
    }
    std::println(
      "  frame {}: dt {:>8}  cpu {:>8}  fps {:6.1f}  budget {}",
      i + 1,
      scaled(dt),
      scaled(cpu),
      frames.fps(),
      over_budget ? "BLOWN" : "ok"
    );
  }

  std::println("\n== 6. Profile report ==");
  std::println(
    "  {:<10}{:>6}{:>12}{:>12}{:>12}{:>12}", "phase", "n", "total", "mean", "min", "max"
  );
  for (auto const& [name, s] : prof.buckets()) {
    std::println(
      "  {:<10}{:>6}{:>12}{:>12}{:>12}{:>12}",
      name,
      s.count,
      scaled(s.total),
      scaled(s.mean()),
      scaled(s.min),
      scaled(s.max)
    );
  }

  std::println("\n== Summary ==");
  std::println("  frames run         {}", frames.frame_count());
  std::println("  blown budgets      {}", blown_budgets);
  std::println("  intro completed    {}", intro_done);
  std::println(
    "  simulated run time {}", ch::format(clk::now().time_since_epoch(), "{m}m:{s}s.{ms}")
  );

  std::println("\nThat is the timing spine of a frame loop: a frame timer for the");
  std::println("delta and FPS, a rate limiter to pace it, scope timers feeding a");
  std::println("profiler, a deadline + stopwatch for the budget, a countdown for a");
  std::println("timed phase, and the duration formatters for the report.");
  return 0;
}
