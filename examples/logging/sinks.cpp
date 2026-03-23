/**
 * @file
 * @brief Sinks: a custom one, fan-out, the async decorator, and rotation.
 *
 * A sink owns a destination; the library owns the transport. This tour builds
 * the sink side directly (without a manager) so each piece is easy to read:
 *
 *   1. A custom sink             -> derive, override write_out/flush_out.
 *   2. multi_sink                -> fan one record out to several children, each
 *                                   with its own per-sink level filter.
 *   3. async_sink                -> a decorator that offloads a slow sink's
 *                                   writes to a background thread.
 *   4. rotating_file_sink         -> size-based rotation with bounded backups.
 *
 * We drive the sinks by handing them records straight through sink::write (the
 * public entry the backend itself uses), so there is no manager or logger in the
 * way of seeing what each sink does.
 */

#include <cstddef>
#include <cstdio>
#include <memory>
#include <source_location>
#include <string>
#include <utility>

#include <nexenne/logging/async_sink.hpp>
#include <nexenne/logging/multi_sink.hpp>
#include <nexenne/logging/rotating_file_sink.hpp>
#include <nexenne/logging/sink.hpp>

namespace lg = nexenne::logging;

// A custom sink that keeps only a running count and the highest severity seen -
// a cheap "health summary" you might expose on a status endpoint. It consumes
// the structured record, so it never pays to format a line.
class summary_sink final : public lg::sink {
public:
  [[nodiscard]] auto count() const noexcept -> std::size_t {
    return m_count;
  }

  [[nodiscard]] auto worst() const noexcept -> lg::level {
    return m_worst;
  }

protected:
  auto write_out(lg::record const& r) noexcept -> void override {
    m_count += 1;
    if (r.severity > m_worst) {
      m_worst = r.severity;
    }
  }

  auto flush_out() noexcept -> void override {}

private:
  std::size_t m_count{0};
  lg::level m_worst{lg::level::trace};
};

// Builds a record at a given level. A real logger does this for you and fills in
// the timestamp/thread; here we forge them so the tour needs no logger. The name
// is a string literal (process-lifetime), satisfying the record's borrow rule.
[[nodiscard]] auto make(lg::level const sev, std::string msg) -> lg::record {
  return lg::record{sev, std::source_location::current(), "demo", std::move(msg)};
}

auto main() -> int {
  // 1. The custom sink on its own. write() applies the sink's own level filter
  // (default trace, so everything passes) before calling our write_out.
  std::puts("== 1. Custom summary_sink ==");
  summary_sink summary;
  summary.write(make(lg::level::info, "started"));
  summary.write(make(lg::level::warn, "degraded"));
  summary.write(make(lg::level::error, "request failed"));
  std::printf(
    "  saw %zu records; worst severity = %s\n",
    summary.count(),
    lg::to_string(summary.worst()).data()
  );

  // 2. Fan-out with per-child filters. A multi_sink owns its children and
  // dispatches in insertion order; each child keeps its own min_level, so one
  // record can reach some children and be dropped by others. Here a console
  // child (warn and up) and a ring child (everything) split the same stream.
  std::puts("== 2. multi_sink fan-out with per-child filters ==");
  lg::multi_sink fan;
  auto console{std::make_unique<lg::console_sink>(lg::console_sink::stream::stdout_only)};
  console->set_min_level(lg::level::warn);  // console only shows warn and up
  auto ring{std::make_unique<lg::ring_sink<8>>()};
  auto* const ring_ptr{ring.get()};  // borrow to read the snapshot back later
  fan.add(std::move(console));
  fan.add(std::move(ring));
  std::printf("  fan has %zu children\n", fan.child_count());
  fan.write(make(lg::level::info, "info: ring only, not console"));
  fan.write(make(lg::level::warn, "warn: both console and ring"));
  fan.flush();
  std::printf("  ring captured %zu line(s) (it kept the info too)\n", ring_ptr->size());

  // 3. The async_sink decorator. It wraps a slow inner sink and inserts a
  // background thread: producers enqueue cheaply and return, the thread drains.
  // The overflow policy decides behaviour on a full queue (block / drop_oldest /
  // drop_newest). The destructor drains gracefully - no queued record is lost on
  // a normal teardown - so we scope it and read the inner summary afterward.
  std::puts("== 3. async_sink decorator (offloads writes) ==");
  summary_sink* inner_view{nullptr};
  {
    auto inner{std::make_unique<summary_sink>()};
    inner_view = inner.get();
    lg::async_sink offloaded{
      std::move(inner),
      lg::async_sink::config{.queue_size_limit = 64, .on_overflow = lg::overflow_action::block}
    };
    for (int i{0}; i < 50; ++i) {
      offloaded.write(make(lg::level::info, "async record"));
    }
    offloaded.flush();  // block until the queue drains once
  }  // async_sink destructor joins the worker, so inner has seen every record now
  std::printf("  inner summary_sink saw %zu records after the drain\n", inner_view->count());

  // 4. rotating_file_sink: cap each file at max_bytes and keep max_files
  // backups. We set a tiny cap so a handful of lines forces a rotation, then
  // report where the bytes are. The rotation check runs before any write that
  // would cross the limit, so a record is never split across two files.
  std::puts("== 4. rotating_file_sink (size-based rotation) ==");
  auto const base{std::string{"showcase_rotate.log"}};
  {
    lg::rotating_file_sink rot{base, /*max_bytes=*/256, /*max_files=*/2};
    if (rot.is_open()) {
      for (int i{0}; i < 12; ++i) {
        rot.write(make(lg::level::info, "rotating line number " + std::to_string(i)));
      }
      rot.flush();
      std::printf(
        "  base=%.*s active file now holds %zu byte(s) after rotation\n",
        static_cast<int>(rot.base_path().size()),
        rot.base_path().data(),
        rot.current_size()
      );
    } else {
      std::puts("  could not open the rotating log file (skipping)");
    }
  }
  // Tidy up the files this tour wrote so reruns start clean.
  static_cast<void>(std::remove(base.c_str()));
  static_cast<void>(std::remove((base + ".1").c_str()));
  static_cast<void>(std::remove((base + ".2").c_str()));
  std::puts("  (rotated files cleaned up)");
  return 0;
}
