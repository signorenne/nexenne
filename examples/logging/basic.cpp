/**
 * @file
 * @brief End-to-end logging: register sinks on the manager and emit records.
 *
 * Shows the normal front end (a named basic_logger fanning out through the
 * manager to a console sink and an in-memory ring), the LOG_* macros on the
 * default logger, and the heap-free stream_logger for an embedded-style path.
 */

#include <cstdio>

#include <nexenne/logging/logging.hpp>

auto main() -> int {
  namespace lg = nexenne::logging;

  // Wire a console sink and a crash-diagnostics ring onto the default manager.
  auto& mgr{lg::manager::instance()};
  mgr.add_sink(std::make_shared<lg::console_sink>());
  auto ring{std::make_shared<lg::ring_sink<8>>()};
  mgr.add_sink(ring);

  // A named logger sharing the default config (async backend thread).
  lg::logger net{"net"};
  net.info("listening on {}:{}", "0.0.0.0", 8080);
  net.warn("slow client {}", 42);

  // The macros target the default logger and gate on NEXENNE_LOG_MIN_LEVEL.
  LOG_ERROR("unhandled signal {}", 11);

  mgr.flush();  // drain the async queue before reading the ring back

  std::puts("--- last lines retained in the ring ---");
  for (auto const& line : ring->snapshot()) {
    std::fputs(line.c_str(), stdout);
  }

  // Heap-free path: no manager, no allocation, straight to stderr. The output
  // is a compile-time writer (file_writer here); an MCU can plug in a UART/RTT
  // writer instead with zero call overhead and no FILE*.
  lg::stream_logger boot{"boot", lg::level::info, lg::file_writer{stderr}};
  boot.info("clock = {} MHz", 240);

  return 0;
}
