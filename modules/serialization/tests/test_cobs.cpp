#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <vector>

#include <nexenne/serialization/cobs.hpp>

namespace {

namespace cobs = nexenne::serialization::cobs;

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

auto bytes(std::initializer_list<int> vs) -> std::vector<std::byte> {
  std::vector<std::byte> out;
  for (int const v : vs)
    out.push_back(static_cast<std::byte>(v));
  return out;
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
  CHECK(r.error() == nexenne::serialization::error::buffer_full);
}

TEST_CASE("cobs: decode rejects a zero byte in the frame") {
  auto const bad{bytes({3, 1, 0, 2})};  // contains an illegal 0x00
  std::array<std::byte, 8> out{};
  auto const r{cobs::decode(bad, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == nexenne::serialization::error::invalid_input);
}

TEST_CASE("cobs: decode reports underrun on a truncated frame") {
  auto const truncated{bytes({5, 1, 2})};  // code says 4 data bytes, only 2 present
  std::array<std::byte, 8> out{};
  auto const r{cobs::decode(truncated, out)};
  REQUIRE_FALSE(r);
  CHECK(r.error() == nexenne::serialization::error::buffer_underrun);
}

}  // namespace
