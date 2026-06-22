/**
 * @file
 * @brief A guided tour of nexenne::chrono through one realistic task: the timing
 *        spine of a game / render loop with a built-in profiler.
 *
 * This program does not draw anything - it *runs the clock* a real engine would,
 * and prints the timings, so you can see how the module's pieces fit together in
 * context:
 *
 *   1. Drive the loop      -> frame_timer for the per-frame delta and FPS.
 *   2. Cap the frame rate  -> rate_limiter as a "may I render now?" gate.
 *   3. Profile the phases  -> scope_timer feeding per-name buckets in a profiler.
 *   4. Budget one frame    -> stopwatch + deadline to catch a frame that overran.
 *   5. Run a timed phase   -> countdown for a fixed "intro" segment of the run.
 *   6. Report              -> duration_parts::format / format_scaled for output.
 *
 * Every nexenne::chrono type is templated on its clock. A shipping engine would
 * use the default std::chrono::steady_clock and let real wall time pass; here we
 * drive a manual_clock by hand so every number below is exactly reproducible -
 * no sleeps, no flakiness, no dependence on how fast the machine is. Swapping
 * `clk` for std::chrono::steady_clock is the only change needed to make this a
 * live loop.
 *
 * Each step notes *why* a given API is the right tool. Read it top to bottom.
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

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

// One hand-advanced clock for the whole simulation. A distinct tag keeps this
// clock's static state from colliding with any other manual_clock in the build.
using clk = ch::basic_manual_clock<struct showcase_tag>;

// A frozen "work simulator": instead of doing real CPU work and timing it (which
// would be non-deterministic), each phase just advances the manual clock by a
// fixed cost. The scope_timer then measures exactly that advance.
auto burn(clk::duration const cost) noexcept -> void {
  clk::advance(cost);
}

// Pretty-print a duration as an auto-scaled single unit (us / ms / ...). Pass a
// double-based nanosecond duration so format_scaled keeps fractional precision;
// an integer ms duration would round 4170 us down to "4 ms".
auto scaled(clk::duration const d) -> std::string {
  return ch::format_scaled(std::chrono::duration_cast<std::chrono::duration<double, std::nano>>(d));
}

}  // namespace

auto main() -> int {
  clk::reset();

  // The per-frame costs of three engine phases, in the order a frame runs them.
  // Frame 3 deliberately spikes (a GC pause, an asset load) so the budget check
  // in step 4 has something to catch.
  struct frame_plan {
    clk::duration update;
    clk::duration physics;
    clk::duration render;
  };

  constexpr std::array<frame_plan, 6> plan{{
    {1200us, 800us, 3000us},
    {1100us, 820us, 3200us},
    {1300us, 760us, 3100us},
    {1250us, 900us, 9000us},  // the spike: render blows the budget
    {1180us, 810us, 2950us},
    {1220us, 780us, 3050us},
  }};

  // 1. The frame timer.
  //
  // frame_timer::tick() returns the delta since the previous frame and folds it
  // into a moving window, so fps() reports a smoothed recent average rather than
  // one jittery sample. The first tick() has no previous frame, so it returns
  // zero and just establishes the baseline - never divide by it. The window size
  // (here 4) is a template parameter: small enough to react, large enough to
  // smooth.
  std::println("== 1. Frame loop ==");
  ch::frame_timer<clk, 4> frames;

  // 2. The frame-rate cap.
  //
  // A token bucket is a clean throttle: one token == permission to start one
  // frame. capacity 1 with refill 200/sec means "at most ~1 frame per 5 ms, but
  // allow a single frame's worth of slack to absorb jitter". We poll
  // until_next_token() to find how long we'd sleep, then (since this is a manual
  // clock) advance time by exactly that much instead of really sleeping. In a
  // live loop you would std::this_thread::sleep_for(wait) here instead.
  ch::rate_limiter<clk> gate{1.0, 200.0};

  // 3. The profiler.
  //
  // The profiler aggregates timed scopes by name. We ask it once per phase for a
  // sink(name) - a cheap callable that caches a pointer to that name's stats
  // bucket - and hand the sink to a scope_timer. When the scope_timer leaves its
  // block it fires the sink with the elapsed duration, which lands in the bucket.
  // No std::function indirection, no per-sample map lookup, no allocation after
  // the first use of each name.
  ch::profiler<clk> prof;
  auto update_sink{prof.sink("update")};
  auto physics_sink{prof.sink("physics")};
  auto render_sink{prof.sink("render")};

  // The CPU budget for one frame at a 120 fps target: each frame must finish
  // inside ~8.33 ms. We arm a deadline per frame against it; the spike frame
  // (frame 4, ~11 ms of work) is the one that overruns.
  constexpr auto frame_budget{8333us};
  std::uint64_t blown_budgets{0};

  // 5 (set up first, used in the loop). The intro countdown.
  //
  // A countdown fires true exactly once on the running-to-expired transition.
  // We use it to mark when the run leaves its "intro" phase (the first 10 ms of
  // simulated time), e.g. to swap a loading screen for gameplay.
  ch::countdown<clk> intro{10ms};
  intro.start();
  bool intro_done{false};

  for (std::size_t i{0}; i < plan.size(); ++i) {
    // 2 (continued). Throttle: ask the bucket when a token will be ready and
    // skip ahead to that instant. try_acquire() then succeeds because we waited
    // exactly long enough. On a real clock this is the frame-pacing sleep.
    auto const wait{gate.until_next_token()};
    if (wait > clk::duration::zero()) {
      clk::advance(wait);
    }
    static_cast<void>(gate.try_acquire());

    auto const dt{frames.tick()};

    // 4. The per-frame budget guard. A deadline is the right tool for an
    // absolute "must be done by" instant: arm it once at frame start, then ask
    // reached() / remaining() against the live clock without re-deriving the
    // target. A stopwatch measures the frame's own wall time in parallel.
    auto const budget{ch::deadline<clk>::after(frame_budget)};
    ch::stopwatch<clk> frame_sw;
    frame_sw.start();

    auto const& f{plan[i]};

    // 3 (continued). Each phase is wrapped in a scope_timer bound to its sink.
    // The braces matter: the timer fires when *its* block ends, so each phase is
    // measured independently. Note the explicit clock template argument -
    // scope_timer defaults its clock to steady_clock, but we want the manual one.
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

    // 5 (continued). Poll the intro countdown each frame; it returns true on the
    // single frame where simulated time first passes 10 ms.
    if (intro.tick()) {
      intro_done = true;
      std::println("  frame {}: intro phase complete, gameplay begins", i + 1);
    }

    // 4 (continued). The frame is done; did it fit its budget? deadline.reached()
    // compares the live clock against the armed target. The stopwatch tells us by
    // how much, and remaining() reports the slack that was left (zero once over).
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

  // 6. The report.
  //
  // Iterate the profiler's buckets (a std::map, so names come out sorted) and
  // print each phase's count, total, mean, min, and max. format_scaled keeps
  // sub-millisecond resolution, which is exactly what a micro-timing report
  // wants - unlike the d/h/m/s breakdown, which would round these away.
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
  // The whole simulated run, formatted through the d/h/m/s/ms breakdown, which is
  // the right pick for a wall-clock total a human reads ("how long did the run
  // take") rather than a per-call micro-timing. clk::now() is the time since the
  // epoch we reset() to, i.e. the total simulated time advanced this run.
  std::println(
    "  simulated run time {}", ch::format(clk::now().time_since_epoch(), "{m}m:{s}s.{ms}")
  );

  std::println("\nThat is the timing spine of a frame loop: a frame timer for the");
  std::println("delta and FPS, a rate limiter to pace it, scope timers feeding a");
  std::println("profiler, a deadline + stopwatch for the budget, a countdown for a");
  std::println("timed phase, and the duration formatters for the report.");
  return 0;
}
