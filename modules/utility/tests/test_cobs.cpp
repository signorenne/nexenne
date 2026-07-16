#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <vector>

#include <nexenne/utility/cobs.hpp>

namespace {

namespace cobs = nexenne::utility::cobs;
using cobs::error;

// Round-trip helper: encode then decode and compare to the original.
auto round_trip(std::vector<std::byte> const& payload) -> std::vector<std::byte> {
  std::vector<std::byte> encoded(cobs::max_encoded_size(payload.size()));
  auto const enc{cobs::encode(payload, encoded)};
  REQUIRE(enc);
  encoded.resize(*enc);

  // The encoded form must never contain a zero byte (so 0x00 can delimit frames).
  for (auto const b : encoded) {
    CHECK(b != std::byte{0});
  }

  std::vector<std::byte> decoded(payload.size() + 1);
  auto const dec{cobs::decode(encoded, decoded)};
  REQUIRE(dec);
  decoded.resize(*dec);
  return decoded;
}

// Encode into a max-sized buffer and return the encoded frame on its own.
auto encode_frame(std::vector<std::byte> const& payload) -> std::vector<std::byte> {
  std::vector<std::byte> encoded(cobs::max_encoded_size(payload.size()));
  auto const enc{cobs::encode(payload, encoded)};
  REQUIRE(enc);
  encoded.resize(*enc);
  return encoded;
}

auto bytes(std::initializer_list<int> vs) -> std::vector<std::byte> {
  std::vector<std::byte> out;
  for (int const v : vs) {
    out.push_back(static_cast<std::byte>(v));
  }
  return out;
}

TEST_CASE("nexenne::utility::cobs round-trips representative payloads") {
  CHECK(round_trip(bytes({})) == bytes({}));
  CHECK(round_trip(bytes({1, 2, 3})) == bytes({1, 2, 3}));
  CHECK(round_trip(bytes({0, 0, 0})) == bytes({0, 0, 0}));
  CHECK(round_trip(bytes({1, 0, 2, 0, 3})) == bytes({1, 0, 2, 0, 3}));
  CHECK(round_trip(bytes({0})) == bytes({0}));
}

TEST_CASE("nexenne::utility::cobs Cheshire-Baker reference vectors") {
  struct entry {
    std::vector<std::byte> raw;
    std::vector<std::byte> encoded;
  };

  auto const cases{std::array<entry, 5>{
    entry{bytes({}), bytes({0x01})},
    entry{bytes({0x00}), bytes({0x01, 0x01})},
    entry{bytes({0x00, 0x00}), bytes({0x01, 0x01, 0x01})},
    entry{bytes({0x11, 0x22, 0x00, 0x33}), bytes({0x03, 0x11, 0x22, 0x02, 0x33})},
    entry{bytes({0x11, 0x22, 0x33, 0x44}), bytes({0x05, 0x11, 0x22, 0x33, 0x44})},
  }};
  for (auto const& c : cases) {
    CHECK(encode_frame(c.raw) == c.encoded);

    std::vector<std::byte> dec(c.raw.size() + 1);
    auto const dr{cobs::decode(c.encoded, dec)};
    REQUIRE(dr);
    dec.resize(*dr);
    CHECK(dec == c.raw);
  }
}

TEST_CASE("nexenne::utility::cobs round-trips runs spanning the 0xFF boundary") {
  for (std::size_t n{250}; n <= 262; ++n) {
    std::vector<std::byte> run(n, std::byte{0x99});
    CHECK(round_trip(run) == run);
  }
}

TEST_CASE("nexenne::utility::cobs encodes a maximal 254-byte run") {
  std::vector<std::byte> run(254, std::byte{0x42});
  auto const frame{encode_frame(run)};
  // 0xFF code + 254 data bytes + a trailing 0x01 code byte for the empty block.
  CHECK(frame.size() == 256);
  CHECK(frame[0] == std::byte{0xFF});
  CHECK(frame[255] == std::byte{0x01});
  CHECK(round_trip(run) == run);
}

TEST_CASE("nexenne::utility::cobs round-trips a 255-byte run (block-overflow)") {
  std::vector<std::byte> run(255, std::byte{0x42});
  auto const frame{encode_frame(run)};
  CHECK(frame.size() == 257);
  CHECK(frame[0] == std::byte{0xFF});
  CHECK(frame[255] == std::byte{0x02});
  CHECK(round_trip(run) == run);
}

TEST_CASE("nexenne::utility::cobs encode reports output_too_small") {
  auto const payload{bytes({1, 2, 3, 4})};
  std::array<std::byte, 2> tiny{};
  auto const r{cobs::encode(payload, tiny)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::output_too_small);

  // A zero-length output buffer is rejected before the first write.
  std::array<std::byte, 0> none{};
  auto const r0{cobs::encode(bytes({1}), none)};
  REQUIRE_FALSE(r0);
  CHECK(r0.error() == error::output_too_small);
}

TEST_CASE("nexenne::utility::cobs encode into an exact-size buffer succeeds") {
  // Every capacity strictly below the exact encoded size must error; the exact
  // size (which may be under max_encoded_size) must succeed.
  auto const payload{bytes({1, 0, 2, 3, 0, 4, 5, 6})};
  auto const exact{encode_frame(payload).size()};
  for (std::size_t cap{0}; cap < exact; ++cap) {
    std::vector<std::byte> out(cap);
    auto const r{cobs::encode(payload, out)};
    REQUIRE_FALSE(r);
    CHECK(r.error() == error::output_too_small);
  }
  std::vector<std::byte> out(exact);
  auto const ok{cobs::encode(payload, out)};
  REQUIRE(ok);
  CHECK(*ok == exact);
}

TEST_CASE("nexenne::utility::cobs decode rejects a zero byte in the frame") {
  auto const bad{bytes({3, 1, 0, 2})};  // contains an illegal 0x00
  std::array<std::byte, 8> out{};
  auto const r{cobs::decode(bad, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::invalid_input);

  // A lone zero code byte is also rejected.
  auto const lone{bytes({0})};
  auto const r2{cobs::decode(lone, out)};
  REQUIRE_FALSE(r2);
  CHECK(r2.error() == error::invalid_input);
}

TEST_CASE("nexenne::utility::cobs decode rejects an embedded 0x00 at every position") {
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

TEST_CASE("nexenne::utility::cobs decode reports truncated_input on a short frame") {
  auto const truncated{bytes({5, 1, 2})};  // code says 4 data bytes, only 2 present
  std::array<std::byte, 8> out{};
  auto const r{cobs::decode(truncated, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::truncated_input);

  // A leading code byte that overruns the frame length.
  auto const overrun{bytes({10, 1, 2, 3})};
  auto const r2{cobs::decode(overrun, out)};
  REQUIRE_FALSE(r2);
  CHECK(r2.error() == error::truncated_input);
}

TEST_CASE("nexenne::utility::cobs decode reports output_too_small") {
  auto const payload{bytes({1, 0, 2, 0, 3, 0, 4})};
  auto const frame{encode_frame(payload)};
  std::vector<std::byte> out(payload.size() - 1);  // one byte short
  auto const r{cobs::decode(frame, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == error::output_too_small);
}

TEST_CASE("nexenne::utility::cobs decode of an empty frame yields an empty payload") {
  std::array<std::byte, 4> out{};
  std::span<std::byte const> const empty{};
  auto const r{cobs::decode(empty, out)};
  REQUIRE(r);
  CHECK(*r == 0);
}

TEST_CASE("nexenne::utility::cobs encode is usable at compile time") {
  static constexpr auto frame{[] {
    auto in{
      std::array<std::byte, 4>{std::byte{0x11}, std::byte{0x22}, std::byte{0x00}, std::byte{0x33}}
    };
    auto out{std::array<std::byte, 8>{}};
    auto const n{cobs::encode(std::span<std::byte const>{in}, std::span<std::byte>{out})};
    return std::pair{out, n.has_value() ? *n : std::size_t{0}};
  }()};
  static_assert(frame.second == 5);
  static_assert(frame.first[0] == std::byte{0x03});
  static_assert(frame.first[3] == std::byte{0x02});
  CHECK(frame.second == 5);
}

TEST_CASE("nexenne::utility::cobs every random-ish payload round-trips and stays zero-free") {
  std::uint32_t state{0x12345678u};
  auto next{[&state]() -> std::uint8_t {
    state = state * 1664525u + 1013904223u;
    return static_cast<std::uint8_t>(state >> 24);
  }};
  for (int trial{0}; trial < 64; ++trial) {
    std::size_t const n{static_cast<std::size_t>(next()) * 3};  // up to ~765 bytes
    std::vector<std::byte> payload;
    payload.reserve(n);
    for (std::size_t i{0}; i < n; ++i) {
      payload.push_back(static_cast<std::byte>(next()));
    }
    CHECK(round_trip(payload) == payload);
  }
}

TEST_CASE("nexenne::utility::cobs to_string and formatter name every error") {
  CHECK(cobs::to_string(error::invalid_input) == "invalid_input");
  CHECK(cobs::to_string(error::truncated_input) == "truncated_input");
  CHECK(cobs::to_string(error::output_too_small) == "output_too_small");
  CHECK(std::format("{}", error::invalid_input) == "invalid_input");
  CHECK(std::format("{:>17}", error::truncated_input) == "  truncated_input");
}

}  // namespace
