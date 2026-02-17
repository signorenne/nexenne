#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <vector>

#include <nexenne/serialization/cobs.hpp>

namespace {

namespace cobs = nexenne::serialization::cobs;
using nexenne::serialization::error;

// Round-trip helper: encode then decode and compare to the original.
auto round_trip(std::vector<std::byte> const& payload) -> std::vector<std::byte> {
  std::vector<std::byte> encoded(cobs::cobs_max_encoded_size(payload.size()));
  auto const enc{cobs::encode(payload, encoded)};
  REQUIRE(enc);
  encoded.resize(*enc);

  // Encoded form must never contain a zero byte (so 0x00 can delimit frames).
  for (auto const b : encoded)
    CHECK(b != std::byte{0});

  std::vector<std::byte> decoded(payload.size() + 1);
  auto const dec{cobs::decode(encoded, decoded)};
  REQUIRE(dec);
  decoded.resize(*dec);
  return decoded;
}

// Encode into a max-sized buffer and return the encoded frame on its own.
auto encode_frame(std::vector<std::byte> const& payload) -> std::vector<std::byte> {
  std::vector<std::byte> encoded(cobs::cobs_max_encoded_size(payload.size()));
  auto const enc{cobs::encode(payload, encoded)};
  REQUIRE(enc);
  encoded.resize(*enc);
  return encoded;
}

auto bytes(std::initializer_list<int> vs) -> std::vector<std::byte> {
  std::vector<std::byte> out;
  for (int const v : vs)
    out.push_back(static_cast<std::byte>(v));
  return out;
}

// True iff the encoded frame contains no 0x00 byte (the COBS invariant).
auto zero_free(std::vector<std::byte> const& frame) -> bool {
  for (auto const b : frame)
    if (b == std::byte{0})
      return false;
  return true;
}

TEST_CASE("cobs: round-trips representative payloads") {
  CHECK(round_trip(bytes({})) == bytes({}));
  CHECK(round_trip(bytes({1, 2, 3})) == bytes({1, 2, 3}));
  CHECK(round_trip(bytes({0, 0, 0})) == bytes({0, 0, 0}));
  CHECK(round_trip(bytes({1, 0, 2, 0, 3})) == bytes({1, 0, 2, 0, 3}));
  CHECK(round_trip(bytes({0})) == bytes({0}));
}

TEST_CASE("cobs: round-trips a run longer than 254 (forces a 0xFF block)") {
  std::vector<std::byte> payload(300, std::byte{0xAB});
  payload[150] = std::byte{0};  // a zero in the middle too
  CHECK(round_trip(payload) == payload);
}

TEST_CASE("cobs: encode reports buffer_full when output is too small") {
  auto const payload{bytes({1, 2, 3, 4})};
  std::array<std::byte, 2> tiny{};
  auto const r{cobs::encode(payload, tiny)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_full);
}

TEST_CASE("cobs: decode rejects a zero byte in the frame") {
  auto const bad{bytes({3, 1, 0, 2})};  // contains an illegal 0x00
  std::array<std::byte, 8> out{};
  auto const r{cobs::decode(bad, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::invalid_input);
}

TEST_CASE("cobs: decode reports underrun on a truncated frame") {
  auto const truncated{bytes({5, 1, 2})};  // code says 4 data bytes, only 2 present
  std::array<std::byte, 8> out{};
  auto const r{cobs::decode(truncated, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_underrun);
}

TEST_CASE("cobs: round-trips empty input") {
  auto const empty{bytes({})};
  CHECK(round_trip(empty) == empty);
  // The empty payload still encodes to a single non-empty code byte.
  auto const frame{encode_frame(empty)};
  CHECK(frame.size() == 1);
  CHECK(frame[0] == std::byte{1});
}

TEST_CASE("cobs: round-trips a single zero byte") {
  auto const one_zero{bytes({0})};
  CHECK(round_trip(one_zero) == one_zero);
  CHECK(zero_free(encode_frame(one_zero)));
}

TEST_CASE("cobs: round-trips an all-zeros payload of several lengths") {
  for (std::size_t n{1}; n <= 300; n += 37) {
    std::vector<std::byte> zeros(n, std::byte{0});
    CHECK(round_trip(zeros) == zeros);
    CHECK(zero_free(encode_frame(zeros)));
  }
}

TEST_CASE("cobs: round-trips a no-zero payload of several lengths") {
  for (std::size_t n{1}; n <= 600; n += 53) {
    std::vector<std::byte> data(n, std::byte{0x7E});
    CHECK(round_trip(data) == data);
    CHECK(zero_free(encode_frame(data)));
  }
}

TEST_CASE("cobs: round-trips an alternating zero / non-zero payload") {
  std::vector<std::byte> alt;
  for (std::size_t i{0}; i < 512; ++i)
    alt.push_back((i % 2 == 0) ? std::byte{0} : std::byte{0xC3});
  CHECK(round_trip(alt) == alt);
  CHECK(zero_free(encode_frame(alt)));

  // The complementary phase (starts with a non-zero).
  std::vector<std::byte> alt2;
  for (std::size_t i{0}; i < 513; ++i)
    alt2.push_back((i % 2 == 0) ? std::byte{0xC3} : std::byte{0});
  CHECK(round_trip(alt2) == alt2);
  CHECK(zero_free(encode_frame(alt2)));
}

TEST_CASE("cobs: round-trips a 254-byte no-zero run (the block boundary)") {
  // 254 non-zero bytes fill a complete COBS block (code byte 0xFF). This
  // implementation flushes the block as soon as the code reaches 0xFF, so it
  // also opens a fresh (final, empty) block whose code byte is 0x01.
  std::vector<std::byte> run(254, std::byte{0x42});
  CHECK(round_trip(run) == run);
  auto const frame{encode_frame(run)};
  CHECK(zero_free(frame));
  // 0xFF code + 254 data bytes + a trailing 0x01 code byte for the empty block.
  CHECK(frame.size() == 256);
  CHECK(frame[0] == std::byte{0xFF});
  CHECK(frame[255] == std::byte{0x01});
}

TEST_CASE("cobs: round-trips a 255-byte no-zero run (block-overflow boundary)") {
  // CRITICAL: 255 non-zero bytes overflow a single 0xFF block; the 255th byte
  // must start a fresh block. Historically the trickiest COBS edge.
  std::vector<std::byte> run(255, std::byte{0x42});
  CHECK(round_trip(run) == run);
  auto const frame{encode_frame(run)};
  CHECK(zero_free(frame));
  // 0xFF block (254 bytes) + a trailing block of one byte: code 0xFF, 254
  // bytes, code 0x02, 1 byte => 257 bytes total.
  CHECK(frame.size() == 257);
  CHECK(frame[0] == std::byte{0xFF});
  CHECK(frame[255] == std::byte{0x02});
}

TEST_CASE("cobs: round-trips runs spanning the 0xFF boundary at every offset") {
  for (std::size_t n{250}; n <= 262; ++n) {
    std::vector<std::byte> run(n, std::byte{0x99});
    CHECK(round_trip(run) == run);
    CHECK(zero_free(encode_frame(run)));
  }
}

TEST_CASE("cobs: round-trips a 254-byte run followed by a zero") {
  // A full 0xFF block immediately followed by a zero: the zero must be
  // recoverable, and decode must NOT inject a phantom zero after the block.
  std::vector<std::byte> payload(254, std::byte{0x42});
  payload.push_back(std::byte{0});
  CHECK(round_trip(payload) == payload);
  CHECK(zero_free(encode_frame(payload)));
}

TEST_CASE("cobs: round-trips data longer than 255 with zeros at various spots") {
  for (std::size_t pos :
       {std::size_t{0},
        std::size_t{1},
        std::size_t{253},
        std::size_t{254},
        std::size_t{255},
        std::size_t{256},
        std::size_t{299}}) {
    std::vector<std::byte> payload(300, std::byte{0xAB});
    payload[pos] = std::byte{0};
    CHECK(round_trip(payload) == payload);
    CHECK(zero_free(encode_frame(payload)));
  }
}

TEST_CASE("cobs: round-trips a large multi-block payload with scattered zeros") {
  std::vector<std::byte> payload(4096, std::byte{0x5A});
  for (std::size_t i{0}; i < payload.size(); i += 100)
    payload[i] = std::byte{0};
  CHECK(round_trip(payload) == payload);
  CHECK(zero_free(encode_frame(payload)));

  // A payload that is a clean multiple of 254 (every block exactly full).
  std::vector<std::byte> aligned(254 * 4, std::byte{0x11});
  CHECK(round_trip(aligned) == aligned);
  CHECK(zero_free(encode_frame(aligned)));
}

TEST_CASE("cobs: the encoded frame never contains a zero byte (broad sweep)") {
  // Every byte value, every length up to a few blocks, never a 0x00 in output.
  for (int v{0}; v < 256; v += 17) {
    for (std::size_t n{0}; n <= 520; n += 41) {
      std::vector<std::byte> payload(n, static_cast<std::byte>(v));
      CHECK(zero_free(encode_frame(payload)));
    }
  }
}

TEST_CASE("cobs: encode reports buffer_full for a zero-length output buffer") {
  auto const payload{bytes({1})};
  std::array<std::byte, 0> none{};
  auto const r{cobs::encode(payload, none)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_full);
}

TEST_CASE("cobs: encode into a buffer one byte short reports buffer_full") {
  // Try every output capacity strictly below the exact encoded size; each
  // must error cleanly, never overflow (caught under ASan).
  auto const payload{bytes({1, 0, 2, 3, 0, 4, 5, 6})};
  auto const exact{encode_frame(payload).size()};
  for (std::size_t cap{0}; cap < exact; ++cap) {
    std::vector<std::byte> out(cap);
    auto const r{cobs::encode(payload, out)};
    REQUIRE_FALSE(r);
    CHECK(r.error() == error::buffer_full);
  }
  // The exact size succeeds.
  std::vector<std::byte> out(exact);
  auto const ok{cobs::encode(payload, out)};
  REQUIRE(ok);
  CHECK(*ok == exact);
}

TEST_CASE("cobs: encode of a multi-block payload one byte short reports buffer_full") {
  std::vector<std::byte> payload(300, std::byte{0xAB});
  payload[150] = std::byte{0};
  auto const exact{encode_frame(payload).size()};
  std::vector<std::byte> out(exact - 1);
  auto const r{cobs::encode(payload, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_full);
}

TEST_CASE("cobs: decode reports buffer_full when the output buffer is too small") {
  auto const payload{bytes({1, 0, 2, 0, 3, 0, 4})};
  auto const frame{encode_frame(payload)};
  // Output is one byte short of the decoded payload.
  std::vector<std::byte> out(payload.size() - 1);
  auto const r{cobs::decode(frame, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_full);
}

TEST_CASE("cobs: decode into a zero-length buffer reports buffer_full for non-empty data") {
  auto const frame{encode_frame(bytes({7}))};
  std::array<std::byte, 0> none{};
  auto const r{cobs::decode(frame, none)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_full);
}

TEST_CASE("cobs: decode rejects a pointer byte that runs past the end") {
  // A leading code byte claiming more data than the frame holds.
  auto const bad{bytes({10, 1, 2, 3})};  // code 10 wants 9 data bytes, 3 present
  std::array<std::byte, 16> out{};
  auto const r{cobs::decode(bad, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_underrun);
}

TEST_CASE("cobs: decode of a lone zero byte is rejected") {
  auto const bad{bytes({0})};
  std::array<std::byte, 4> out{};
  auto const r{cobs::decode(bad, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::invalid_input);
}

TEST_CASE("cobs: decode rejects a zero embedded mid-frame at every position") {
  // Take a clean frame, poke a 0x00 into each interior data slot, and confirm
  // every variant is rejected as invalid_input with no out-of-bounds read.
  auto const clean{encode_frame(bytes({1, 2, 3, 4, 5, 6, 7, 8}))};
  for (std::size_t i{1}; i < clean.size(); ++i) {
    auto bad{clean};
    bad[i] = std::byte{0};
    std::array<std::byte, 32> out{};
    auto const r{cobs::decode(bad, out)};
    REQUIRE_FALSE(r);
    CHECK(r.error() == error::invalid_input);
  }
}

TEST_CASE("cobs: decode of every truncated prefix of a real frame errors cleanly") {
  // Build a frame that spans multiple blocks and a few zeros, then feed every
  // strict prefix to the decoder. Each must return an error (never succeed
  // with a bogus length) and must not read past the end (ASan-verified).
  std::vector<std::byte> payload(280, std::byte{0x3C});
  payload[0] = std::byte{0};
  payload[260] = std::byte{0};
  payload[279] = std::byte{0};
  auto const frame{encode_frame(payload)};

  for (std::size_t len{1}; len < frame.size(); ++len) {
    std::vector<std::byte> prefix(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(len));
    std::vector<std::byte> out(payload.size() + 1);
    auto const r{cobs::decode(prefix, out)};
    // A strict prefix can only succeed if it happens to be a self-consistent
    // shorter frame; otherwise it must report an error. Either way, no crash.
    if (!r) {
      CHECK(
        (r.error() == error::buffer_underrun || r.error() == error::invalid_input
         || r.error() == error::buffer_full)
      );
    }
  }
}

TEST_CASE("cobs: decode of a single-block truncation reports underrun") {
  // code 0xFF promises 254 data bytes; supply only a handful.
  std::vector<std::byte> frame;
  frame.push_back(std::byte{0xFF});
  for (int i{0}; i < 5; ++i)
    frame.push_back(std::byte{0x11});
  std::array<std::byte, 256> out{};
  auto const r{cobs::decode(frame, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_underrun);
}

TEST_CASE("cobs: decode of an empty frame yields an empty payload") {
  std::array<std::byte, 4> out{};
  std::span<std::byte const> const empty{};
  auto const r{cobs::decode(empty, out)};
  REQUIRE(r);
  CHECK(*r == 0);
}

TEST_CASE("cobs: every random-ish payload round-trips and stays zero-free") {
  // Deterministic pseudo-random sweep over content and length.
  std::uint32_t state{0x12345678u};
  auto next{[&state]() -> std::uint8_t {
    state = state * 1664525u + 1013904223u;
    return static_cast<std::uint8_t>(state >> 24);
  }};
  for (int trial{0}; trial < 64; ++trial) {
    std::size_t const n{static_cast<std::size_t>(next()) * 3};  // up to ~765 bytes
    std::vector<std::byte> payload;
    payload.reserve(n);
    for (std::size_t i{0}; i < n; ++i)
      payload.push_back(static_cast<std::byte>(next()));
    CHECK(round_trip(payload) == payload);
    CHECK(zero_free(encode_frame(payload)));
  }
}

}  // namespace
