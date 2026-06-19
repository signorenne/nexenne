#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <nexenne/serialization/serialization.hpp>

namespace {

using namespace nexenne::serialization;

// A payload struct and a version-dispatching codec used across the
// decode_with cases. Version 1 reads a single u32; version 2 reads two u16
// halves and recombines them; any other version is rejected by the codec.
struct sample {
  std::uint16_t version{};
  std::uint32_t payload{};
};

struct sample_codec {
  auto
  decode(binary::reader& r, std::uint16_t const v) const noexcept -> std::expected<sample, error> {
    if (v == 1) {
      auto const p{r.read<std::uint32_t>()};
      if (!p)
        return std::unexpected{p.error()};
      return sample{.version = v, .payload = *p};
    }
    if (v == 2) {
      auto const lo{r.read<std::uint16_t>()};
      if (!lo)
        return std::unexpected{lo.error()};
      auto const hi{r.read<std::uint16_t>()};
      if (!hi)
        return std::unexpected{hi.error()};
      return sample{.version = v, .payload = (static_cast<std::uint32_t>(*hi) << 16) | *lo};
    }
    return std::unexpected{error::invalid_input};
  }
};

TEST_CASE("nexenne::serialization::versioned - envelope round trip carries magic, version, payload"
) {
  constexpr std::uint32_t magic{0x4E455845};
  std::array<std::byte, 64> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 7).has_value());
  REQUIRE(w.write(std::uint32_t{42}).has_value());

  // Header is exactly 8 bytes; payload follows immediately.
  CHECK(w.bytes_written() == versioned_header_size + sizeof(std::uint32_t));

  binary::reader r{w.written()};
  auto const h{read_header(r, magic)};
  REQUIRE(h.has_value());
  CHECK(h->magic == magic);
  CHECK(h->version == 7u);
  CHECK(r.position() == versioned_header_size);  // cursor left at the body
  CHECK(*r.read<std::uint32_t>() == 42u);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::versioned - read_header surfaces the parsed version exactly") {
  constexpr std::uint32_t magic{0x4E455835};
  std::array<std::byte, 32> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 513).has_value());  // 0x0201 exercises both bytes

  binary::reader r{w.written()};
  auto const h{read_header(r, magic)};
  REQUIRE(h.has_value());
  CHECK(h->magic == magic);
  CHECK(h->version == 513u);
  CHECK(r.position() == versioned_header_size);
}

TEST_CASE("nexenne::serialization::versioned - version edge values round trip") {
  for (std::uint16_t const v :
       {std::uint16_t{0},
        std::uint16_t{1},
        std::uint16_t{255},
        std::uint16_t{256},
        std::uint16_t{0x7FFF},
        std::uint16_t{0xFFFF}}) {
    constexpr std::uint32_t magic{0xABCD1234};
    std::array<std::byte, 16> buf{};
    binary::writer w{buf};
    REQUIRE(write_header(w, magic, v).has_value());

    binary::reader r{w.written()};
    auto const h{read_header(r, magic)};
    REQUIRE(h.has_value());
    CHECK(h->version == v);
  }
}

TEST_CASE("nexenne::serialization::versioned - magic edge values round trip") {
  for (std::uint32_t const m :
       {std::uint32_t{0},
        std::uint32_t{1},
        std::uint32_t{0xFFFFFFFF},
        std::uint32_t{0x4E455845},
        std::uint32_t{0x00FF00FF}}) {
    std::array<std::byte, 16> buf{};
    binary::writer w{buf};
    REQUIRE(write_header(w, m, 3).has_value());

    binary::reader r{w.written()};
    auto const h{read_header(r, m)};
    REQUIRE(h.has_value());
    CHECK(h->magic == m);
    CHECK(h->version == 3u);
  }
}

TEST_CASE(
  "nexenne::serialization::versioned - header bytes are little-endian with a zeroed reserved field"
) {
  constexpr std::uint32_t magic{0x44332211};
  constexpr std::uint16_t version{0x6655};
  std::array<std::byte, versioned_header_size> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, version).has_value());

  auto const raw{w.written()};
  REQUIRE(raw.size() == versioned_header_size);
  // magic, little-endian.
  CHECK(raw[0] == std::byte{0x11});
  CHECK(raw[1] == std::byte{0x22});
  CHECK(raw[2] == std::byte{0x33});
  CHECK(raw[3] == std::byte{0x44});
  // version, little-endian.
  CHECK(raw[4] == std::byte{0x55});
  CHECK(raw[5] == std::byte{0x66});
  // reserved is zeroed.
  CHECK(raw[6] == std::byte{0});
  CHECK(raw[7] == std::byte{0});
}

TEST_CASE("nexenne::serialization::versioned - read_header tolerates a non-zero reserved field") {
  // The reserved field is not surfaced and must not be validated, so a frame
  // with reserved bytes set still parses (forward-compatibility contract).
  constexpr std::uint32_t magic{0x01020304};
  std::array<std::byte, versioned_header_size> hdr{
    std::byte{0x04},
    std::byte{0x03},
    std::byte{0x02},
    std::byte{0x01},  // magic LE
    std::byte{0x09},
    std::byte{0x00},  // version = 9
    std::byte{0xAB},
    std::byte{0xCD}  // reserved (non-zero)
  };
  binary::reader r{std::span<std::byte const>{hdr}};
  auto const h{read_header(r, magic)};
  REQUIRE(h.has_value());
  CHECK(h->magic == magic);
  CHECK(h->version == 9u);
}

TEST_CASE("nexenne::serialization::versioned - wrong magic is rejected with invalid_input") {
  constexpr std::uint32_t magic{0xCAFEBABE};
  std::array<std::byte, 16> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 1).has_value());

  binary::reader r{w.written()};
  auto const h{read_header(r, 0xDEADBEEF)};
  CHECK(!h.has_value());
  CHECK(h.error() == error::invalid_input);
}

TEST_CASE("nexenne::serialization::versioned - a single-bit magic difference is rejected") {
  constexpr std::uint32_t magic{0x00000000};
  std::array<std::byte, 16> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 1).has_value());

  binary::reader r{w.written()};
  auto const h{read_header(r, 0x00000001)};  // one bit off
  CHECK(!h.has_value());
  CHECK(h.error() == error::invalid_input);
}

TEST_CASE(
  "nexenne::serialization::versioned - read_header reports buffer_underrun on a short envelope"
) {
  constexpr std::uint32_t magic{0x4E455834};
  // Every length below the 8-byte header must be rejected, never read OOB.
  for (std::size_t n{0}; n < versioned_header_size; ++n) {
    std::vector<std::byte> buf(n);
    binary::reader r{std::span<std::byte const>{buf.data(), buf.size()}};
    auto const h{read_header(r, magic)};
    REQUIRE_FALSE(h.has_value());
    CHECK(h.error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::versioned - a truncated payload after a valid header errors at "
          "the body read") {
  constexpr std::uint32_t magic{0x4E455845};
  // A full header but only 2 of the 4 payload bytes present.
  std::array<std::byte, versioned_header_size + 2> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 1).has_value());
  REQUIRE(w.write(std::uint16_t{0xBEEF}).has_value());  // only half the u32 the codec wants

  binary::reader r{w.written()};
  auto const h{read_header(r, magic)};
  REQUIRE(h.has_value());  // header itself is intact
  auto const v{r.read<std::uint32_t>()};
  REQUIRE_FALSE(v.has_value());
  CHECK(v.error() == error::buffer_underrun);
}

TEST_CASE(
  "nexenne::serialization::versioned - write_header reports buffer_full when the writer lacks room"
) {
  for (std::size_t cap{0}; cap < versioned_header_size; ++cap) {
    std::vector<std::byte> buf(cap);
    binary::writer w{std::span<std::byte>{buf.data(), buf.size()}};
    auto const r{write_header(w, 0x11223344, 5)};
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);  // failed write leaves the cursor untouched
  }
  // Exactly 8 bytes is enough.
  std::array<std::byte, versioned_header_size> exact{};
  binary::writer w{exact};
  CHECK(write_header(w, 0x11223344, 5).has_value());
}

TEST_CASE("nexenne::serialization::versioned - decode_with dispatches to the v1 codec path") {
  constexpr std::uint32_t magic{0x4E455831};
  std::array<std::byte, 32> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 1).has_value());
  REQUIRE(w.write(std::uint32_t{0xCAFEBABE}).has_value());

  binary::reader r{w.written()};
  auto const out{decode_with(r, magic, sample_codec{})};
  REQUIRE(out.has_value());
  CHECK(out->version == 1u);
  CHECK(out->payload == 0xCAFEBABEu);
}

TEST_CASE("nexenne::serialization::versioned - decode_with dispatches to the v2 codec path") {
  constexpr std::uint32_t magic{0x4E455832};
  std::array<std::byte, 32> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 2).has_value());
  REQUIRE(w.write(std::uint16_t{0xBABE}).has_value());  // lo
  REQUIRE(w.write(std::uint16_t{0xCAFE}).has_value());  // hi

  binary::reader r{w.written()};
  auto const out{decode_with(r, magic, sample_codec{})};
  REQUIRE(out.has_value());
  CHECK(out->version == 2u);
  CHECK(out->payload == 0xCAFEBABEu);  // hi << 16 | lo
}

TEST_CASE(
  "nexenne::serialization::versioned - decode_with rejects an unknown version through the codec"
) {
  constexpr std::uint32_t magic{0x4E455833};
  std::array<std::byte, 32> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 99).has_value());
  REQUIRE(w.write(std::uint32_t{0}).has_value());

  binary::reader r{w.written()};
  auto const out{decode_with(r, magic, sample_codec{})};
  CHECK(!out.has_value());
  CHECK(out.error() == error::invalid_input);  // codec rejected version 99
}

TEST_CASE(
  "nexenne::serialization::versioned - decode_with rejects a wrong magic before calling the codec"
) {
  constexpr std::uint32_t stamped{0x11112222};
  std::array<std::byte, 32> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, stamped, 1).has_value());
  REQUIRE(w.write(std::uint32_t{0x12345678}).has_value());

  binary::reader r{w.written()};
  auto const out{decode_with(r, 0x99998888, sample_codec{})};
  CHECK(!out.has_value());
  CHECK(out.error() == error::invalid_input);  // magic mismatch repackaged
  // The header is read to inspect the magic, so the cursor sits just past it;
  // the codec never ran, so the 4-byte payload after the header is untouched.
  CHECK(r.position() == versioned_header_size);
}

TEST_CASE(
  "nexenne::serialization::versioned - decode_with reports buffer_underrun on a truncated envelope"
) {
  constexpr std::uint32_t magic{0x4E455834};
  // Only 4 bytes available - smaller than the 8-byte header.
  std::array<std::byte, 4> buf{};
  binary::reader r{std::span<std::byte const>{buf}};
  auto const out{decode_with(r, magic, sample_codec{})};
  CHECK(!out.has_value());
  CHECK(out.error() == error::buffer_underrun);
}

TEST_CASE(
  "nexenne::serialization::versioned - decode_with propagates a truncated body through the codec"
) {
  constexpr std::uint32_t magic{0x4E455836};
  // Valid v1 header, but the u32 body is one byte short.
  std::array<std::byte, versioned_header_size + 3> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 1).has_value());
  REQUIRE(w.write_bytes(std::array<std::byte, 3>{}).has_value());

  binary::reader r{w.written()};
  auto const out{decode_with(r, magic, sample_codec{})};
  CHECK(!out.has_value());
  CHECK(out.error() == error::buffer_underrun);  // codec's read<u32> ran out
}

}  // namespace
