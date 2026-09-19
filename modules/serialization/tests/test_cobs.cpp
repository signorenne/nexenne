#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <vector>

#include <nexenne/serialization/cobs.hpp>

namespace {

namespace cobs = nexenne::serialization::cobs;
using nexenne::serialization::error;

/**
 * @brief Encodes \p payload, checks the frame is zero-free, and decodes it back.
 *
 * Also checks that the encoded form holds no zero byte, so 0x00 can delimit
 * frames.
 *
 * @param payload Bytes to round-trip.
 *
 * @return The decoded bytes, to compare against \p payload.
 *
 * @pre None.
 * @post One doctest check per encoded byte has been recorded.
 */
auto round_trip(std::vector<std::byte> const& payload) -> std::vector<std::byte> {
  std::vector<std::byte> encoded(cobs::max_encoded_size(payload.size()));
  auto const enc{cobs::encode(payload, encoded)};
  REQUIRE(enc);
  encoded.resize(*enc);

  for (auto const b : encoded)
    CHECK(b != std::byte{0});

  std::vector<std::byte> decoded(payload.size() + 1);
  auto const dec{cobs::decode(encoded, decoded)};
  REQUIRE(dec);
  decoded.resize(*dec);
  return decoded;
}

/**
 * @brief Encodes \p payload into a max-sized buffer and trims it to the frame.
 *
 * @param payload Bytes to encode.
 *
 * @return The encoded frame on its own.
 *
 * @pre None.
 * @post None.
 */
auto encode_frame(std::vector<std::byte> const& payload) -> std::vector<std::byte> {
  std::vector<std::byte> encoded(cobs::max_encoded_size(payload.size()));
  auto const enc{cobs::encode(payload, encoded)};
  REQUIRE(enc);
  encoded.resize(*enc);
  return encoded;
}

/**
 * @brief Builds a byte vector from integer literals.
 *
 * @param vs Byte values, in order.
 *
 * @return The bytes.
 *
 * @pre Every value fits in a byte.
 * @post None.
 */
auto bytes(std::initializer_list<int> vs) -> std::vector<std::byte> {
  std::vector<std::byte> out;
  for (int const v : vs)
    out.push_back(static_cast<std::byte>(v));
  return out;
}

/**
 * @brief Whether \p frame holds no 0x00 byte, the COBS invariant.
 *
 * @param frame Encoded frame to scan.
 *
 * @return \c true when no byte is zero.
 *
 * @pre None.
 * @post None.
 */
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
  payload[150] = std::byte{0};
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
  auto const bad{bytes({3, 1, 0, 2})};
  std::array<std::byte, 8> out{};
  auto const r{cobs::decode(bad, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::invalid_input);
}

TEST_CASE("cobs: decode reports underrun on a truncated frame") {
  auto const truncated{bytes({5, 1, 2})};
  std::array<std::byte, 8> out{};
  auto const r{cobs::decode(truncated, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::buffer_underrun);
}

TEST_CASE("cobs: round-trips empty input") {
  auto const empty{bytes({})};
  CHECK(round_trip(empty) == empty);
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

  std::vector<std::byte> alt2;
  for (std::size_t i{0}; i < 513; ++i)
    alt2.push_back((i % 2 == 0) ? std::byte{0xC3} : std::byte{0});
  CHECK(round_trip(alt2) == alt2);
  CHECK(zero_free(encode_frame(alt2)));
}

TEST_CASE("cobs: round-trips a 254-byte no-zero run (the block boundary)") {
  // The block flushes when its code reaches 0xFF, so an empty final block (0x01) follows.
  std::vector<std::byte> run(254, std::byte{0x42});
  CHECK(round_trip(run) == run);
  auto const frame{encode_frame(run)};
  CHECK(zero_free(frame));
  CHECK(frame.size() == 256);
  CHECK(frame[0] == std::byte{0xFF});
  CHECK(frame[255] == std::byte{0x01});
}

TEST_CASE("cobs: round-trips a 255-byte no-zero run (block-overflow boundary)") {
  std::vector<std::byte> run(255, std::byte{0x42});
  CHECK(round_trip(run) == run);
  auto const frame{encode_frame(run)};
  CHECK(zero_free(frame));
  // Code 0xFF and 254 bytes, then code 0x02 and 1 byte: 257 bytes.
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

  std::vector<std::byte> aligned(254 * 4, std::byte{0x11});
  CHECK(round_trip(aligned) == aligned);
  CHECK(zero_free(encode_frame(aligned)));
}

TEST_CASE("cobs: the encoded frame never contains a zero byte (broad sweep)") {
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
  auto const payload{bytes({1, 0, 2, 3, 0, 4, 5, 6})};
  auto const exact{encode_frame(payload).size()};
  for (std::size_t cap{0}; cap < exact; ++cap) {
    std::vector<std::byte> out(cap);
    auto const r{cobs::encode(payload, out)};
    REQUIRE_FALSE(r);
    CHECK(r.error() == error::buffer_full);
  }
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
  auto const bad{bytes({10, 1, 2, 3})};
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
  std::vector<std::byte> payload(280, std::byte{0x3C});
  payload[0] = std::byte{0};
  payload[260] = std::byte{0};
  payload[279] = std::byte{0};
  auto const frame{encode_frame(payload)};

  for (std::size_t len{1}; len < frame.size(); ++len) {
    std::vector<std::byte> prefix(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(len));
    std::vector<std::byte> out(payload.size() + 1);
    auto const r{cobs::decode(prefix, out)};
    if (!r) {
      CHECK(
        (r.error() == error::buffer_underrun || r.error() == error::invalid_input
         || r.error() == error::buffer_full)
      );
    }
  }
}

TEST_CASE("cobs: decode of a single-block truncation reports underrun") {
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

TEST_CASE("cobs: cobs_max_encoded_size alias equals max_encoded_size") {
  for (std::size_t n :
       {std::size_t{0},
        std::size_t{1},
        std::size_t{253},
        std::size_t{254},
        std::size_t{255},
        std::size_t{1000}}) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    CHECK(cobs::cobs_max_encoded_size(n) == cobs::max_encoded_size(n));
#pragma GCC diagnostic pop
  }
}

TEST_CASE("cobs: every random-ish payload round-trips and stays zero-free") {
  // Numerical Recipes LCG; the top byte drives a deterministic sweep.
  std::uint32_t state{0x12345678u};
  auto next{[&state]() -> std::uint8_t {
    state = state * 1664525u + 1013904223u;
    return static_cast<std::uint8_t>(state >> 24);
  }};
  for (int trial{0}; trial < 64; ++trial) {
    std::size_t const n{static_cast<std::size_t>(next()) * 3};
    std::vector<std::byte> payload;
    payload.reserve(n);
    for (std::size_t i{0}; i < n; ++i)
      payload.push_back(static_cast<std::byte>(next()));
    CHECK(round_trip(payload) == payload);
    CHECK(zero_free(encode_frame(payload)));
  }
}

}  // namespace
