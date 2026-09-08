/**
 * @file
 * @brief Write and then parse a tiny binary message with a buffer_cursor.
 *
 * A sensor message is encoded as [magic byte][reading count][one 16-bit
 * big-endian value per reading]. A write cursor fills the buffer with put and
 * take, a read cursor walks it back with peek, next, and take, and every step
 * is guarded by has() or remaining() so neither side can run off the end.
 *
 *   1. The encoder stops cleanly when the buffer is full. \c take hands back the
 *      next two bytes as a dynamic-extent span, and \c first<2>() restores the
 *      fixed extent the endian helper needs.
 *   2. The decoder, a cursor over const bytes, peeks at the magic byte so a
 *      wrong one consumes nothing, then reads the count and each value.
 *   3. Decoding a truncated view of the same message reports the cut through
 *      the has() guard instead of reading past the end.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <vector>

#include <nexenne/utility/buffer_cursor.hpp>
#include <nexenne/utility/endian.hpp>

namespace util = nexenne::utility;

namespace {

constexpr std::byte magic{0xA5};

auto encode(std::span<std::byte> const storage, std::vector<std::uint16_t> const& readings)
  -> std::span<std::byte> {
  auto writer{util::buffer_cursor{storage}};
  writer.put(magic);
  writer.put(static_cast<std::byte>(readings.size()));
  for (auto const reading : readings) {
    if (!writer.has(2)) {
      break;
    }
    util::write_be(writer.take(2).first<2>(), reading);
  }
  return writer.consumed();
}

auto decode(std::span<std::byte const> const message) -> void {
  auto reader{util::buffer_cursor{message}};
  if (!reader.has(2)) {
    std::println("truncated message: header missing");
    return;
  }
  if (reader.peek(1)[0] != magic) {
    std::println("bad magic byte");
    return;
  }
  reader.advance(1);

  auto const count{std::to_integer<std::size_t>(reader.next())};
  std::println("message claims {} readings, {} bytes remain", count, reader.remaining());

  for (auto i{std::size_t{0}}; i < count; ++i) {
    if (!reader.has(2)) {
      std::println("truncated message: reading {} missing", i);
      return;
    }
    auto const value{util::read_be<std::uint16_t>(reader.take(2).first<2>())};
    std::println("reading {} = {}", i, value);
  }
  std::println("done, position {} of {}", reader.position(), reader.size());
}

}  // namespace

auto main() -> int {
  auto storage{std::array<std::byte, 16>{}};
  std::vector<std::uint16_t> const readings{512, 1024, 65535};

  auto const message{encode(std::span{storage}, readings)};
  std::println("encoded {} bytes", message.size());
  decode(message);

  decode(std::span<std::byte const>{message}.first(5));
  return 0;
}
