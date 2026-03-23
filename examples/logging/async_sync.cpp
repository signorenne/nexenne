/**
 * @file
 * @brief Async vs synchronous backends, chosen by the compile-time config.
 *
 * Every templated logger/manager takes a config carrying queue_size and async.
 * basic_manager<Config> resolves to one of two backends sharing one public API:
 *
 *   - async (the default): a lock-free queue plus a backend thread. push()
 *     returns immediately; the thread fans each record out to the sinks. A full
 *     queue drops the record and bumps dropped_count - logging never blocks the
 *     hot path. Records may be dispatched slightly after the producer moves on,
 *     so you flush() before reading a sink back.
 *   - sync: no thread, no queue. Every push dispatches on the calling thread, in
 *     program order. Right for init-time logging, a single-core MCU, and tests
 *     that need deterministic ordering.
 *
 * Each distinct Config has its own manager and default_logger singletons, so the
 * sync world here is fully independent of the default async one. This tour wires
 * both, shows ordering, the LOG_*_TO macros, and the async drop counter.
 */

#include <cstdio>
#include <memory>

#include <nexenne/logging/logging.hpp>

namespace lg = nexenne::logging;

// A tiny synchronous config: async=false picks the sync backend; queue_size is
// ignored when async is false (there is no queue), so any value is fine.
using sync_config = lg::config<8, false>;
using sync_logger = lg::basic_logger<sync_config>;
using sync_manager = lg::basic_manager<sync_config>;

// A deliberately tiny async queue (size 2) so we can overflow it on purpose and
// watch dropped_count move. queue_size must be a power of two in async mode.
using tiny_async_config = lg::config<2, true>;
using tiny_async_logger = lg::basic_logger<tiny_async_config>;
using tiny_async_manager = lg::basic_manager<tiny_async_config>;

auto main() -> int {
  // 1. Synchronous: dispatch happens inline on this thread, in program order, so
  // no flush is needed to see output and the lines cannot interleave. is_async
  // is a static constexpr on the backend, queryable without an instance.
  std::puts("== 1. Synchronous backend (inline, in order) ==");
  std::printf("  sync_manager::is_async = %s\n", sync_manager::is_async ? "true" : "false");
  auto& smgr{sync_manager::instance()};
  smgr.add_sink(std::make_shared<lg::console_sink>(lg::console_sink::stream::stdout_only));
  sync_logger boot{"boot"};
  for (int step{1}; step <= 3; ++step) {
    boot.info("init step {} (printed before the next push returns)", step);
  }

  // 2. The default async backend. push returns at once; the backend thread does
  // the writing, so we flush() before trusting that everything landed. The
  // LOG_*_TO macros target a supplied logger and add the compile-time gate on
  // NEXENNE_LOG_MIN_LEVEL on top of the logger's runtime gate.
  std::puts("== 2. Asynchronous backend (queued, then flushed) ==");
  std::printf("  manager::is_async = %s\n", lg::manager::is_async ? "true" : "false");
  auto& amgr{lg::manager::instance()};
  amgr.add_sink(std::make_shared<lg::console_sink>(lg::console_sink::stream::stdout_only));
  lg::logger worker{"worker"};
  LOG_INFO_TO(worker, "job {} accepted", 1001);
  LOG_WARN_TO(worker, "job {} retrying ({} of {})", 1001, 2, 5);
  amgr.flush();  // block until the backend has dispatched the two records above
  std::puts("  (flushed: both async lines are now guaranteed written)");

  // 3. Overflowing a tiny async queue. push() is non-blocking and returns a
  // std::expected; on a full queue it drops the record and bumps dropped_count
  // rather than stalling the producer. We pour records in faster than the
  // backend can drain a 2-slot queue, then flush and report the drops. (The
  // exact count is timing-dependent; the point is that overflow is observable,
  // not fatal.)
  std::puts("== 3. Async queue overflow (non-blocking drop) ==");
  auto& tmgr{tiny_async_manager::instance()};
  tmgr.add_sink(std::make_shared<lg::ring_sink<4>>());  // a slow-enough sink target
  tiny_async_logger flood{"flood"};
  for (int i{0}; i < 2000; ++i) {
    flood.info("burst record {}", i);  // many of these will not fit the 2-slot queue
  }
  tmgr.flush();
  std::printf(
    "  pushed 2000 records through a 2-slot queue; dropped at least %zu\n", tmgr.dropped_count()
  );
  std::printf("  sync backend never drops: dropped_count = %zu\n", sync_manager::dropped_count());

  // 4. Force a clean async shutdown. The destructor does this automatically at
  // program exit, but calling it explicitly drains the queue, joins the backend
  // thread, and flushes the sinks one final time - handy before a hard restart.
  std::puts("== 4. Explicit shutdown (drain + join + flush) ==");
  tmgr.shutdown();
  std::puts("  tiny async manager drained and its backend joined");
  return 0;
}
