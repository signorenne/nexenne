/**
 * @file
 * @brief Frame two packets on a byte stream with COBS and decode them back.
 *
 * Each payload may contain any byte, zeros included. COBS rewrites it into a
 * zero-free form, so a single 0x00 appended after each frame delimits the
 * stream unambiguously. The reader splits the stream on 0x00 and decodes every
 * frame back to its exact original bytes.
 */

#include <array>
#include <cstddef>
#include <print>
#include <span>
#include <vector>

#include <nexenne/utility/cobs.hpp>

namespace cobs = nexenne::utility::cobs;

namespace {

// Encodes one payload and appends the 0x00 frame delimiter to the stream.
auto frame_into(std::vector<std::byte>& stream, std::span<std::byte const> const payload) -> void {
  std::vector<std::byte> encoded(cobs::max_encoded_size(payload.size()));
  auto const n{cobs::encode(payload, encoded)};
  if (!n) {
    std::println("encode failed: {}", cobs::to_string(n.error()));
    return;
  }
  encoded.resize(*n);
  stream.insert(stream.end(), encoded.begin(), encoded.end());
  stream.push_back(std::byte{0});  // the delimiter, guaranteed absent from the frame
}

// Splits the stream on 0x00 and decodes each frame back to its payload.
auto read_frames(std::span<std::byte const> const stream) -> void {
  std::size_t start{0};
  for (std::size_t i{0}; i < stream.size(); ++i) {
    if (stream[i] != std::byte{0}) {
      continue;
    }
    auto const frame{stream.subspan(start, i - start)};
    std::vector<std::byte> decoded(frame.size());  // never longer than the frame
    auto const n{cobs::decode(frame, decoded)};
    if (!n) {
      std::println("decode failed: {}", cobs::to_string(n.error()));
    } else {
      decoded.resize(*n);
      std::print("frame of {} bytes:", decoded.size());
      for (auto const b : decoded) {
        std::print(" {:02X}", std::to_integer<unsigned>(b));
      }
      std::println("");
    }
    start = i + 1;
  }
}

}  // namespace

auto main() -> int {
  auto const packet_a{std::array<std::byte, 5>{
    std::byte{0x11}, std::byte{0x00}, std::byte{0x22}, std::byte{0x00}, std::byte{0x33}
  }};
  auto const packet_b{std::array<std::byte, 3>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}}};

  std::vector<std::byte> stream;
  frame_into(stream, packet_a);
  frame_into(stream, packet_b);
  std::println("stream is {} bytes across two frames", stream.size());

  read_frames(stream);
  return 0;
}
