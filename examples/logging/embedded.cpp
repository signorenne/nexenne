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
 * The destination is a compile-time Writer - a callable handed the formatted
 * bytes as a std::span<char const>. It defaults to file_writer (a FILE*), but a
 * real MCU plugs in a UART or an RTT channel instead, with zero indirection
 * (the writer inlines) and no FILE* anywhere. This tour shows both: the default
 * FILE* writer, a buffer size small enough to force truncation, and a custom
 * writer that captures the bytes (standing in for a UART register).
 */

#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>

#include <nexenne/logging/stream_logger.hpp>

namespace lg = nexenne::logging;

// A custom Writer standing in for a hardware transport (UART, RTT, a ring in
// SRAM). It must be invocable with std::span<char const> and noexcept; here it
// appends the bytes to an owned string so we can print what "went out the wire".
// On real hardware operator() would push each byte into a peripheral register.
struct capture_writer {
  std::string* sink{nullptr};

  auto operator()(std::span<char const> const bytes) const noexcept -> void {
    if (sink != nullptr) {
      sink->append(bytes.data(), bytes.size());
    }
  }
};

auto main() -> int {
  // 1. The default path: write straight to a FILE* with the default 256-byte
  // buffer. No allocation happens on any of these calls. A below-min call (the
  // debug here, with min == info) early-outs before it touches the buffer.
  std::puts("== 1. Default FILE* writer ==");
  lg::stream_logger boot{"boot", lg::level::info, lg::file_writer{stdout}};
  boot.debug("probing flash (gated: below boot min level)");
  boot.info("clock={} MHz heap={} KiB", 240, 320);
  boot.warn("brownout threshold near: {} mV", 3300);

  // 2. Retargeting the writer at runtime. file_writer holds a public FILE*, so
  // writer().stream can be swapped to redirect output (here, harmlessly, to the
  // same stream). On hardware this is how you move logs from a boot UART to the
  // application's channel once it is up.
  std::puts("== 2. Retarget the writer ==");
  boot.writer().stream = stdout;
  boot.info("retargeted writer still works");

  // 3. Truncation. A small buffer forces format_to_n to stop at the boundary;
  // the logger overwrites the last three bytes with "..." so the reader sees the
  // line was clipped rather than silently losing the tail. The minimum buffer is
  // 32 bytes (enforced by the type), barely larger than the prefix, so a long
  // message is guaranteed to truncate here.
  std::puts("== 3. Truncation in a tiny buffer ==");
  lg::basic_stream_logger<lg::file_writer, 48> tiny{
    "tiny", lg::level::trace, lg::file_writer{stdout}
  };
  tiny.info("this message is far longer than the 48-byte stack buffer allows");

  // 4. A custom Writer: capture the bytes instead of writing to a FILE*. This is
  // the seam an MCU uses for a UART/RTT transport - no FILE*, no heap, the write
  // inlines. We pass the writer by value at construction; it points at a string
  // we own so we can show exactly what the logger emitted.
  std::puts("== 4. Custom Writer (captures the bytes) ==");
  std::string wire;
  lg::basic_stream_logger<capture_writer> uart{"uart", lg::level::trace, capture_writer{&wire}};
  uart.info("temp={}C rh={}%", 21, 47);
  uart.error("i2c nack on addr=0x{:02x}", 0x3c);
  std::printf("  captured %zu bytes:\n", wire.size());
  std::fputs(wire.c_str(), stdout);  // already newline-terminated per call
  return 0;
}
