/**
 * @file
 * @brief Severity levels: the names, the ordering, and the two filter layers.
 *
 * The module filters a log call at three independent points, all keyed on the
 * level ordering (trace < debug < info < warn < error < critical < off):
 *
 *   1. Compile time, via NEXENNE_LOG_MIN_LEVEL: a LOG_* macro below the
 *      threshold expands to nothing, so it costs zero runtime and zero binary.
 *   2. Per logger, at runtime: a call below the logger's min_level early-outs
 *      with one relaxed atomic load, before any formatting.
 *   3. Per sink, at runtime: a sink drops records below its own min_level, so
 *      two sinks on one logger can keep different amounts of detail.
 *
 * This tour shows the names, then layers (2) and (3) without a manager so the
 * decisions are easy to read. Layer (1) is shown in the embedded and async
 * tours where the LOG_* macros appear.
 */

#include <cstdio>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/stream_logger.hpp>

namespace lg = nexenne::logging;

auto main() -> int {
  // The full ladder, most verbose first. to_string pads to five characters so
  // columns line up; to_char gives the compact single-letter tag.
  std::puts("== Levels, verbose to severe ==");
  for (auto const l :
       {lg::level::trace,
        lg::level::debug,
        lg::level::info,
        lg::level::warn,
        lg::level::error,
        lg::level::critical}) {
    // The numeric value is the ordering key the filters compare against.
    std::printf(
      "  [%c] %.*s (value %u)\n",
      lg::to_char(l),
      5,
      lg::to_string(l).data(),
      static_cast<unsigned>(l)
    );
  }

  // Layer 2: the per-logger runtime gate. A stream_logger writing to stdout lets
  // us see exactly which calls survive. Setting min to warn drops trace/debug/
  // info at the logger - they never reach the buffer or the writer.
  std::puts("== Per-logger gate (min = WARN) ==");
  lg::stream_logger app{"app", lg::level::warn, lg::file_writer{stdout}};
  app.debug("setup detail (dropped: below WARN)");
  app.info("ready (dropped: below WARN)");
  app.warn("disk at {}%", 91);           // survives: WARN >= WARN
  app.error("write failed: {}", "EIO");  // survives: ERROR >= WARN

  // enabled() answers the gate without logging, so an expensive-to-build message
  // can be skipped entirely when the level is off.
  std::puts("== Querying the gate before building a message ==");
  std::printf("  app.enabled(INFO)  = %s\n", app.enabled(lg::level::info) ? "true" : "false");
  std::printf("  app.enabled(ERROR) = %s\n", app.enabled(lg::level::error) ? "true" : "false");

  // Raising verbosity at runtime is a single relaxed store; the same logger now
  // lets info through.
  app.set_min_level(lg::level::info);
  std::puts("== After set_min_level(INFO) ==");
  app.info("verbosity raised: this info line now survives");

  // off silences a logger entirely without removing the call sites.
  app.set_min_level(lg::level::off);
  app.critical("even CRITICAL is dropped while min == OFF");
  std::puts("  (a CRITICAL call was just dropped by min == OFF)");
  return 0;
}
