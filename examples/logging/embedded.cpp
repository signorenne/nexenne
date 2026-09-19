/**
 * @file
 * @brief The heap-free embedded path: stream_logger and a custom Writer.
 *
 * stream_logger is the stripped-down sibling of basic_logger for heap-averse
 * targets (a Cortex-M, an ESP32): the same info/warn/... surface and the same
 * source-location capture, but no manager, no backend thread, no queue, no
 * mutex, and no std::string. Each call formats with std::format_to_n into a
 * stack buffer of compile-time size; an overlong message is truncated at the
 * buffer boundary and marked with "...".
 *
 * The destination is a compile-time Writer: a callable handed the formatted
 * bytes as a std::span<char const>. It defaults to file_writer (a FILE*), but a
 * real MCU plugs in a UART or an RTT channel instead, with zero indirection
 * (the writer inlines) and no FILE* anywhere. The tour, in order:
 *
 *   1. The default FILE* writer with the default 256-byte buffer. No call
 *      allocates, and a call below the minimum level (the debug line) returns
 *      before it touches the buffer.
 *   2. Retargeting: file_writer holds a public FILE*, so writer().stream can be
 *      swapped at runtime, the way a device moves its logs from a boot UART to
 *      the application's channel once that is up.
 *   3. Truncation: a 48-byte buffer (the type enforces at least 32, barely more
 *      than the prefix) stops format_to_n at the boundary, and the logger
 *      overwrites the last three bytes with "..." so a clipped line reads as
 *      clipped rather than silently losing its tail.
 *   4. A custom Writer, capture_writer, standing in for a hardware transport: it
 *      must be noexcept-invocable with std::span<char const>, and here appends
 *      the bytes to a string so the tour can print what went out the wire. On
 *      real hardware its operator() would push each byte into a peripheral
 *      register.
 */

#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>

#include <nexenne/logging/stream_logger.hpp>

namespace lg = nexenne::logging;

struct capture_writer {
  std::string* sink{nullptr};

  auto operator()(std::span<char const> const bytes) const noexcept -> void {
    if (sink != nullptr) {
      sink->append(bytes.data(), bytes.size());
    }
  }
};

auto main() -> int {
  std::puts("== 1. Default FILE* writer ==");
  lg::stream_logger boot{"boot", lg::level::info, lg::file_writer{stdout}};
  boot.debug("probing flash (gated: below boot min level)");
  boot.info("clock={} MHz heap={} KiB", 240, 320);
  boot.warn("brownout threshold near: {} mV", 3300);

  std::puts("== 2. Retarget the writer ==");
  boot.writer().stream = stdout;
  boot.info("retargeted writer still works");

  std::puts("== 3. Truncation in a tiny buffer ==");
  lg::basic_stream_logger<lg::file_writer, 48> tiny{
    "tiny", lg::level::trace, lg::file_writer{stdout}
  };
  tiny.info("this message is far longer than the 48-byte stack buffer allows");

  std::puts("== 4. Custom Writer (captures the bytes) ==");
  std::string wire;
  lg::basic_stream_logger<capture_writer> uart{"uart", lg::level::trace, capture_writer{&wire}};
  uart.info("temp={}C rh={}%", 21, 47);
  uart.error("i2c nack on addr=0x{:02x}", 0x3c);
  std::printf("  captured %zu bytes:\n", wire.size());
  std::fputs(wire.c_str(), stdout);
  return 0;
}
