/**
 * @file
 * @brief A guided tour of nexenne::logging through one realistic task: serving a
 *        handful of requests in a tiny "API gateway" and logging every step.
 *
 * The program does not open a socket - it walks fixed request scenarios through
 * a few subsystems and logs structured events the way a real service would, so
 * you can see how the module's pieces fit together in context:
 *
 *   1. Wire the manager   -> add a console sink and a crash-diagnostics ring.
 *   2. Subsystem loggers  -> one named basic_logger per area, each level-gated.
 *   3. Serve requests     -> emit trace/debug/info/warn/error from the handlers.
 *   4. A custom sink       -> a metrics counter that consumes records, not text.
 *   5. Structured JSON     -> the same records rendered as one JSON object/line.
 *   6. Pattern formatting  -> a hand-shaped console line via pattern_formatter.
 *   7. The embedded path   -> the heap-free stream_logger straight to a stream.
 *   8. Crash dump          -> replay the ring sink's retained tail.
 *
 * Read it top to bottom. The recurring theme is *where the cost goes*: a log
 * call that is gated out (by level) costs at most one relaxed atomic load and no
 * formatting; a call that passes pays one std::format on the calling thread and
 * then a cheap move into the async queue; the embedded path pays neither a heap
 * allocation nor a queue.
 */

#include <cstdio>
#include <memory>
#include <string_view>
#include <utility>

#include <nexenne/logging/logging.hpp>

namespace lg = nexenne::logging;

// A custom sink: instead of writing text anywhere, it tallies records by
// severity. This is the whole reason a sink consumes a `record` and not a
// pre-rendered string - a sink can route on the structured fields (severity,
// logger name, source location) without ever paying to format the line. The
// backend calls write_out serially, so the plain counters need no locking for
// the backend's own writes; reads from main happen after a flush, when the
// backend is idle, so they are safe too.
class metrics_sink final : public lg::sink {
public:
  [[nodiscard]] auto warnings() const noexcept -> std::size_t {
    return m_warn;
  }

  [[nodiscard]] auto errors() const noexcept -> std::size_t {
    return m_error;
  }

protected:
  auto write_out(lg::record const& r) noexcept -> void override {
    // Route on the structured severity field, never on the message text.
    if (r.severity == lg::level::warn) {
      m_warn += 1;
    } else if (r.severity >= lg::level::error) {
      m_error += 1;
    }
  }

  auto flush_out() noexcept -> void override {}

private:
  std::size_t m_warn{0};
  std::size_t m_error{0};
};

// One fixed request to drive through the gateway. A real server would read this
// off the wire; here it is just data so the demo is deterministic.
struct request {
  std::string_view method;
  std::string_view path;
  int user_id;
  bool authorized;
  int db_rows;  // rows a backing query would return; <0 means the query failed
};

auto main() -> int {
  // 1. Wire the global manager. The default config is async, so add_sink and the
  // log calls below hand work to a backend thread; we flush before reading any
  // sink back. The console sink here is stdout_only, so every level prints to
  // stdout (it also offers an auto_split policy that routes warn and up to
  // stderr); the ring keeps the last 16 formatted lines for a crash dump.
  std::puts("== 1. Manager and sinks ==");
  auto& mgr{lg::manager::instance()};
  mgr.add_sink(std::make_shared<lg::console_sink>(lg::console_sink::stream::stdout_only));
  auto ring{std::make_shared<lg::ring_sink<16>>()};
  mgr.add_sink(ring);
  auto metrics{std::make_shared<metrics_sink>()};
  mgr.add_sink(metrics);
  std::printf("  registered %zu sinks on the default manager\n", mgr.sink_count());

  // 2. A named logger per subsystem. The name is interned once (process-lifetime
  // storage), so every record it emits borrows the name as a string_view with no
  // per-call allocation. Each logger carries its own runtime minimum level: the
  // database layer is chatty in this run (debug and up), the rest stay at info,
  // and the cache is deliberately quieted to warn-and-up to show a gate at work.
  std::puts("== 2. Subsystem loggers ==");
  lg::logger net{"net", lg::level::info};
  lg::logger db{"db", lg::level::debug};
  lg::logger auth{"auth", lg::level::info};
  lg::logger cache{"cache", lg::level::warn};
  std::printf(
    "  net=%s db=%s auth=%s cache=%s (min levels)\n",
    lg::to_string(net.min_level()).data(),
    lg::to_string(db.min_level()).data(),
    lg::to_string(auth.min_level()).data(),
    lg::to_string(cache.min_level()).data()
  );

  // 3. Serve a few requests. Each handler logs structured fields through the
  // format string - method, path, ids, counts - rather than concatenating a
  // sentence, so a downstream parser (see step 5) can pull the fields back out.
  // Note the db.trace below: it is below db's min level (debug), so it is gated
  // at the logger with a single relaxed load and never formatted.
  std::puts("== 3. Serving requests (console: stdout_only) ==");
  request const requests[]{
    {"GET", "/v1/users/7", 7, true, 1},
    {"GET", "/v1/users/9", 9, false, 0},
    {"POST", "/v1/orders", 7, true, -1},
  };

  for (auto const& req : requests) {
    net.info("recv {} {} from user={}", req.method, req.path, req.user_id);

    // The auth subsystem gates the request. A denial is a warn, not an error:
    // it is expected traffic, but worth surfacing.
    if (!req.authorized) {
      auth.warn("deny user={} on {} (missing scope)", req.user_id, req.path);
      net.info("send 403 for {} {}", req.method, req.path);
      continue;
    }
    auth.info("allow user={} on {}", req.user_id, req.path);

    // The cache is quieted to warn-and-up, so this debug line is dropped at the
    // logger. We still write the call to show the gate: it costs one load.
    cache.debug("lookup {} (this line is gated out)", req.path);

    // The database layer is at debug, so its trace is gated but its debug shows.
    db.trace("open txn for {} (gated: below db min level)", req.path);
    db.debug("query path={} for user={}", req.path, req.user_id);
    if (req.db_rows < 0) {
      db.error("query failed path={} user={} (deadlock, will retry)", req.path, req.user_id);
      net.error("send 500 for {} {}", req.method, req.path);
      continue;
    }
    db.debug("query ok path={} rows={}", req.path, req.db_rows);
    net.info("send 200 for {} {} ({} row(s))", req.method, req.path, req.db_rows);
  }

  // Drain the async queue so every record above has reached every sink before we
  // inspect the metrics sink and the ring. flush() blocks until the backend has
  // dispatched everything this thread pushed, then flushes each sink.
  mgr.flush();

  // 4. Read the custom sink back. It counted severities while the text sinks
  // rendered lines - same records, different consumers, one log call each.
  std::puts("== 4. Metrics sink (counted records, not text) ==");
  std::printf("  warnings=%zu errors=%zu\n", metrics->warnings(), metrics->errors());

  // 5. The same events as structured JSON (NDJSON: one object per line). This is
  // what a log shipper ingests. We give the json_sink an unowned stdout so it
  // does not close our stream, swap the manager's sink set to it alone, and
  // replay one request through a fresh logger so the lines are self-contained.
  std::puts("== 5. Structured JSON (one object per line) ==");
  mgr.clear_sinks();
  mgr.add_sink(std::make_shared<lg::json_sink>(stdout));
  lg::logger api{"api", lg::level::info};
  api.info("request method={} path={} status={}", "GET", "/v1/users/7", 200);
  api.warn("rate limit near for user={} ({}/min)", 7, 58);
  mgr.flush();

  // 6. A pattern_formatter renders a record into a console line of our own shape
  // ("HH:MM:SS.mmm [L] logger | msg"). The default sinks use a fixed layout;
  // this is how you reshape the human-readable line without writing a sink. We
  // build a record by hand to feed it (the same struct the queue carries).
  std::puts("== 6. Pattern-formatted line ==");
  lg::pattern_formatter fmt{"%T [%L] %n | %m"};
  lg::record sample{
    lg::level::info, std::source_location::current(), "report", "daily rollup done"
  };
  std::printf("  %s\n", fmt.format(sample).c_str());

  // 7. The heap-free embedded path. stream_logger has the same call surface but
  // no manager, no queue, no thread, no std::string: each call formats into a
  // stack buffer and hands the bytes to a compile-time Writer (file_writer here,
  // a UART/RTT writer on an MCU) with zero call overhead. A below-min call early
  // -outs before it ever touches the buffer. Truncation past the buffer is
  // marked with "..." rather than allocating - the demonstrated message fits.
  std::puts("== 7. Embedded stream_logger (no heap, no manager) ==");
  lg::stream_logger boot{"boot", lg::level::info, lg::file_writer{stdout}};
  boot.debug("calibrating sensors (gated: below boot min level)");
  boot.info("clock={} MHz heap={} KiB", 240, 320);
  boot.warn("battery low: {} mV", 3300);

  // 8. Crash diagnostics: the ring sink retained the last lines of the run in
  // memory with no file. On a fault you would dump exactly this. We re-attach it
  // and read its snapshot (oldest first); only the most recent 16 lines survive.
  std::puts("== 8. Ring sink replay (last lines retained in memory) ==");
  auto const tail{ring->snapshot()};
  std::printf("  ring holds %zu line(s); showing the last few:\n", tail.size());
  auto const start{tail.size() > 4 ? tail.size() - 4 : std::size_t{0}};
  for (std::size_t i{start}; i < tail.size(); ++i) {
    std::printf("    | %s", tail[i].c_str());  // each line already ends in '\n'
  }

  std::puts("");
  std::puts("That is the module in one request loop: a manager fanning records to");
  std::puts("console/ring/metrics/JSON sinks, per-subsystem level gates, structured");
  std::puts("fields, a custom sink, pattern formatting, and the heap-free path.");
  return 0;
}
