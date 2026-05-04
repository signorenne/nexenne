/**
 * @file
 * @brief Serialize a small packet header in both byte orders and read it back.
 *
 * A fixed-layout header (message type, sequence number, payload length) is
 * encoded big-endian for the wire and little-endian for an on-disk log, then
 * decoded again from each buffer. Every write and read goes through a
 * fixed-extent sub-span, so a wrong byte count or offset is a compile error
 * rather than a runtime over-read.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>

#include <nexenne/utility/endian.hpp>

namespace util = nexenne::utility;

namespace {

// An 8-byte header: 2-byte type, 2-byte sequence, 4-byte payload length.
struct header {
  std::uint16_t type{};
  std::uint16_t sequence{};
  std::uint32_t payload_length{};
};

auto dump(std::span<std::byte const> const bytes) -> void {
  for (auto const b : bytes) {
    std::print(" {:02X}", std::to_integer<unsigned>(b));
  }
  std::println("");
}

}  // namespace

auto main() -> int {
  auto const message{header{.type = 0x0107, .sequence = 42, .payload_length = 512}};

  // Network protocols conventionally send the most significant byte first.
  auto wire{std::array<std::byte, 8>{}};
  auto const out{std::span{wire}};
  util::write_be(out.subspan<0, 2>(), message.type);
  util::write_be(out.subspan<2, 2>(), message.sequence);
  util::write_be(out.subspan<4, 4>(), message.payload_length);
  std::print("big-endian wire:   ");
  dump(wire);

  // The same layout little-endian, as a file format might store it.
  auto disk{std::array<std::byte, 8>{}};
  auto const log{std::span{disk}};
  util::write_le(log.subspan<0, 2>(), message.type);
  util::write_le(log.subspan<2, 2>(), message.sequence);
  util::write_le(log.subspan<4, 4>(), message.payload_length);
  std::print("little-endian log: ");
  dump(disk);

  // Decoding names the type explicitly (there is no value to deduce it from)
  // and uses the same fixed sub-spans, so the offsets live in one place.
  auto const from_wire{header{
    .type = util::read_be<std::uint16_t>(out.subspan<0, 2>()),
    .sequence = util::read_be<std::uint16_t>(out.subspan<2, 2>()),
    .payload_length = util::read_be<std::uint32_t>(out.subspan<4, 4>()),
  }};
  auto const from_disk{header{
    .type = util::read_le<std::uint16_t>(log.subspan<0, 2>()),
    .sequence = util::read_le<std::uint16_t>(log.subspan<2, 2>()),
    .payload_length = util::read_le<std::uint32_t>(log.subspan<4, 4>()),
  }};

  std::println(
    "wire round-trip: type=0x{:04X} sequence={} payload_length={}",
    from_wire.type,
    from_wire.sequence,
    from_wire.payload_length
  );
  std::println(
    "disk round-trip: type=0x{:04X} sequence={} payload_length={}",
    from_disk.type,
    from_disk.sequence,
    from_disk.payload_length
  );

  // The helpers are constexpr, so a layout can be proven at compile time.
  static_assert([] {
    auto buf{std::array<std::byte, 2>{}};
    util::write_be(std::span{buf}, std::uint16_t{0x0107});
    return buf[0] == std::byte{0x01} && buf[1] == std::byte{0x07};
  }());
  return 0;
}
