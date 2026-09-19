#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/serialization/serialization.hpp>

namespace {

using namespace nexenne::serialization;

/**
 * @brief Loads a \c T from the raw bytes at \p off in \p buf.
 *
 * Copies \c sizeof(T) bytes into a default-constructed \c T with \c memcpy,
 * so checking the exact bytes a writer emitted needs no \c reinterpret_cast or
 * strict-aliasing games.
 *
 * @tparam T Trivially copyable type to load.
 * @param buf Buffer holding the bytes.
 * @param off Byte offset of the value in \p buf.
 *
 * @return The value whose object representation is those bytes.
 *
 * @pre \p buf holds at least \c off + sizeof(T) bytes.
 * @post None.
 */
template <typename T>
[[nodiscard]] auto load_le(std::span<std::byte const> const buf, std::size_t const off) -> T {
  auto value{T{}};
  std::memcpy(&value, buf.data() + off, sizeof(T));
  return value;
}

/**
 * @brief Whether \p a and \p b have the same object representation.
 *
 * Bit-exact comparison for floating point, where NaN != NaN under ==.
 *
 * @tparam T Trivially copyable type to compare.
 * @param a First value.
 * @param b Second value.
 *
 * @return \c true when every byte of \p a equals the matching byte of \p b.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
[[nodiscard]] auto bits_equal(T const a, T const b) -> bool {
  auto ba{std::array<std::byte, sizeof(T)>{}};
  auto bb{std::array<std::byte, sizeof(T)>{}};
  std::memcpy(ba.data(), &a, sizeof(T));
  std::memcpy(bb.data(), &b, sizeof(T));
  return ba == bb;
}

/**
 * @brief Views a byte array sized for a \c T as a read-only byte span.
 *
 * @tparam T Type whose size fixes the array length.
 * @param a Bytes to view.
 *
 * @return A span over \p a.
 *
 * @pre None.
 * @post The span aliases \p a.
 */
template <typename T>
[[nodiscard]] auto as_const_bytes(std::array<std::byte, sizeof(T)> const& a)
  -> std::span<std::byte const> {
  return std::span<std::byte const>{a.data(), a.size()};
}

TEST_CASE("nexenne::serialization::binary - round trip every primitive type") {
  auto buf{std::array<std::byte, 256>{}};
  auto w{binary::writer{buf}};

  REQUIRE(w.write(std::uint8_t{0xAB}).has_value());
  REQUIRE(w.write(std::int8_t{-7}).has_value());
  REQUIRE(w.write(std::uint16_t{0xBEEF}).has_value());
  REQUIRE(w.write(std::int16_t{-12345}).has_value());
  REQUIRE(w.write(std::uint32_t{0xDEADBEEF}).has_value());
  REQUIRE(w.write(std::int32_t{-123456789}).has_value());
  REQUIRE(w.write(std::uint64_t{0x0123456789ABCDEFull}).has_value());
  REQUIRE(w.write(std::int64_t{-1234567890123456789ll}).has_value());
  REQUIRE(w.write(true).has_value());
  REQUIRE(w.write(false).has_value());
  REQUIRE(w.write(3.14159F).has_value());
  REQUIRE(w.write(2.718281828459045).has_value());

  auto const expect{
    sizeof(std::uint8_t) + sizeof(std::int8_t) + sizeof(std::uint16_t) + sizeof(std::int16_t)
    + sizeof(std::uint32_t) + sizeof(std::int32_t) + sizeof(std::uint64_t) + sizeof(std::int64_t)
    + 2 * sizeof(bool) + sizeof(float) + sizeof(double)
  };
  CHECK(w.bytes_written() == expect);

  auto r{binary::reader{w.written()}};
  CHECK(*r.read<std::uint8_t>() == 0xAB);
  CHECK(*r.read<std::int8_t>() == -7);
  CHECK(*r.read<std::uint16_t>() == 0xBEEF);
  CHECK(*r.read<std::int16_t>() == -12345);
  CHECK(*r.read<std::uint32_t>() == 0xDEADBEEF);
  CHECK(*r.read<std::int32_t>() == -123456789);
  CHECK(*r.read<std::uint64_t>() == 0x0123456789ABCDEFull);
  CHECK(*r.read<std::int64_t>() == -1234567890123456789ll);
  CHECK(*r.read<bool>() == true);
  CHECK(*r.read<bool>() == false);
  CHECK(bits_equal(*r.read<float>(), 3.14159F));
  CHECK(bits_equal(*r.read<double>(), 2.718281828459045));
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - integers are emitted little-endian") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{0x11223344}).has_value());

  auto const out{w.written()};
  REQUIRE(out.size() == 4);
  CHECK(out[0] == std::byte{0x44});
  CHECK(out[1] == std::byte{0x33});
  CHECK(out[2] == std::byte{0x22});
  CHECK(out[3] == std::byte{0x11});
  CHECK(load_le<std::uint32_t>(out, 0) == 0x11223344u);
}

TEST_CASE("nexenne::serialization::binary - u64 emitted little-endian, all 8 bytes") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint64_t{0x0102030405060708ull}).has_value());

  auto const out{w.written()};
  REQUIRE(out.size() == 8);
  CHECK(out[0] == std::byte{0x08});
  CHECK(out[1] == std::byte{0x07});
  CHECK(out[2] == std::byte{0x06});
  CHECK(out[3] == std::byte{0x05});
  CHECK(out[4] == std::byte{0x04});
  CHECK(out[5] == std::byte{0x03});
  CHECK(out[6] == std::byte{0x02});
  CHECK(out[7] == std::byte{0x01});
}

TEST_CASE("nexenne::serialization::binary - hand-encoded little-endian buffer decodes") {
  auto const raw{std::array<std::byte, 6>{
    std::byte{0x44},
    std::byte{0x33},
    std::byte{0x22},
    std::byte{0x11},
    std::byte{0xEF},
    std::byte{0xBE},
  }};
  auto r{binary::reader{std::span<std::byte const>{raw}}};
  CHECK(*r.read<std::uint32_t>() == 0x11223344u);
  CHECK(*r.read<std::uint16_t>() == 0xBEEFu);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - signed value emitted as twos-complement LE") {
  auto buf{std::array<std::byte, 8>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::int16_t{-2}).has_value());
  auto const out{w.written()};
  CHECK(out[0] == std::byte{0xFE});
  CHECK(out[1] == std::byte{0xFF});
  auto const expect{std::int16_t{-2}};
  CHECK(load_le<std::int16_t>(out, 0) == expect);
}

TEST_CASE("nexenne::serialization::binary - integer boundary values round trip") {
  auto round_u{[](auto const value) {
    using U = decltype(value);
    auto buf{std::array<std::byte, 16>{}};
    auto w{binary::writer{buf}};
    REQUIRE(w.write(value).has_value());
    auto r{binary::reader{w.written()}};
    auto const got{r.read<U>()};
    REQUIRE(got.has_value());
    CHECK(*got == value);
  }};

  round_u(std::uint8_t{0});
  round_u(std::numeric_limits<std::uint8_t>::max());
  round_u(static_cast<std::uint8_t>(std::numeric_limits<std::uint8_t>::max() - 1));
  round_u(std::uint16_t{0});
  round_u(std::numeric_limits<std::uint16_t>::max());
  round_u(std::uint32_t{0});
  round_u(std::numeric_limits<std::uint32_t>::max());
  round_u(std::uint64_t{0});
  round_u(std::numeric_limits<std::uint64_t>::max());

  round_u(std::numeric_limits<std::int8_t>::min());
  round_u(std::numeric_limits<std::int8_t>::max());
  round_u(std::int8_t{-1});
  round_u(std::int8_t{0});
  round_u(std::numeric_limits<std::int16_t>::min());
  round_u(std::numeric_limits<std::int16_t>::max());
  round_u(std::numeric_limits<std::int32_t>::min());
  round_u(std::numeric_limits<std::int32_t>::max());
  round_u(std::numeric_limits<std::int64_t>::min());
  round_u(std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("nexenne::serialization::binary - float/double specials round trip bit-exact") {
  auto round_f{[](auto const value) {
    using F = decltype(value);
    auto buf{std::array<std::byte, 16>{}};
    auto w{binary::writer{buf}};
    REQUIRE(w.write(value).has_value());
    auto r{binary::reader{w.written()}};
    auto const got{r.read<F>()};
    REQUIRE(got.has_value());
    CHECK(bits_equal(*got, value));
  }};

  round_f(std::numeric_limits<float>::quiet_NaN());
  round_f(std::numeric_limits<float>::infinity());
  round_f(-std::numeric_limits<float>::infinity());
  round_f(-0.0F);
  round_f(0.0F);
  round_f(std::numeric_limits<float>::denorm_min());
  round_f(std::numeric_limits<float>::min());
  round_f(std::numeric_limits<float>::max());
  round_f(std::numeric_limits<float>::lowest());

  round_f(std::numeric_limits<double>::quiet_NaN());
  round_f(std::numeric_limits<double>::infinity());
  round_f(-std::numeric_limits<double>::infinity());
  round_f(-0.0);
  round_f(0.0);
  round_f(std::numeric_limits<double>::denorm_min());
  round_f(std::numeric_limits<double>::min());
  round_f(std::numeric_limits<double>::max());
  round_f(std::numeric_limits<double>::lowest());
}

TEST_CASE("nexenne::serialization::binary - -0.0 distinct from +0.0 in bytes") {
  auto buf{std::array<std::byte, 8>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(-0.0).has_value());
  auto const out{w.written()};
  CHECK(out[7] == std::byte{0x80});
  for (auto i{std::size_t{0}}; i < 7; ++i)
    CHECK(out[i] == std::byte{0x00});
}

TEST_CASE("nexenne::serialization::binary - fixed bytes round trip") {
  auto const payload{std::array<std::byte, 5>{
    std::byte{0x00}, std::byte{0xFF}, std::byte{0x7F}, std::byte{0x80}, std::byte{0x01}
  }};
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_bytes(std::span<std::byte const>{payload}).has_value());
  CHECK(w.bytes_written() == 5);

  auto r{binary::reader{w.written()}};
  auto const got{r.read_bytes(5)};
  REQUIRE(got.has_value());
  REQUIRE(got->size() == 5);
  for (auto i{std::size_t{0}}; i < 5; ++i)
    CHECK((*got)[i] == payload[i]);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - empty fixed bytes is a no-op success") {
  auto buf{std::array<std::byte, 4>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_bytes(std::span<std::byte const>{}).has_value());
  CHECK(w.bytes_written() == 0);

  auto r{binary::reader{w.written()}};
  auto const got{r.read_bytes(0)};
  REQUIRE(got.has_value());
  CHECK(got->size() == 0);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - strings of varying length round trip") {
  auto buf{std::array<std::byte, 8192>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::string_view{""}).has_value());
  REQUIRE(w.write(std::string_view{"x"}).has_value());
  REQUIRE(w.write(std::string_view{"hello world"}).has_value());
  auto const big{std::string(1000, 'q')};
  REQUIRE(w.write(std::string_view{big}).has_value());
  REQUIRE(w.write(std::string_view{"with\0embedded", 13}).has_value());

  auto r{binary::reader{w.written()}};
  CHECK(*r.read_string() == "");
  CHECK(*r.read_string() == "x");
  CHECK(*r.read_string() == "hello world");
  CHECK(*r.read_string() == big);
  CHECK(*r.read_string() == std::string_view{"with\0embedded", 13});
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - string prefix bytes are a LEB128 varint") {
  auto buf{std::array<std::byte, 64>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::string_view{"ab"}).has_value());
  auto const out{w.written()};
  REQUIRE(out.size() == 3);
  CHECK(out[0] == std::byte{0x02});
  CHECK(out[1] == std::byte{'a'});
  CHECK(out[2] == std::byte{'b'});

  // 200 encodes as (200 & 0x7F) | 0x80 = 0xC8, then 200 >> 7 = 0x01.
  auto buf2{std::array<std::byte, 256>{}};
  auto w2{binary::writer{buf2}};
  auto const body{std::string(200, 'z')};
  REQUIRE(w2.write(std::string_view{body}).has_value());
  auto const out2{w2.written()};
  CHECK(out2[0] == std::byte{0xC8});
  CHECK(out2[1] == std::byte{0x01});
  CHECK(out2.size() == 202);
}

TEST_CASE("nexenne::serialization::binary - varint length thresholds") {
  struct thr {
    std::uint64_t value;
    std::size_t bytes;
  };

  auto const cases{std::array<thr, 12>{{
    {0, 1},
    {127, 1},
    {128, 2},
    {16383, 2},
    {16384, 3},
    {2097151, 3},
    {2097152, 4},
    {268435455, 4},
    {268435456, 5},
    {0xFFFFFFFFull, 5},
    {0x7FFFFFFFFFFFFFFFull, 9},
    {0xFFFFFFFFFFFFFFFFull, 10},
  }}};

  for (auto const& c : cases) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{binary::writer{buf}};
    REQUIRE(w.write_varint(c.value).has_value());
    CHECK(w.bytes_written() == c.bytes);
    auto r{binary::reader{w.written()}};
    auto const got{r.read_varint()};
    REQUIRE(got.has_value());
    CHECK(*got == c.value);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::binary - varint exact byte image of 300") {
  // 300 = 0b1_0010_1100 -> low 7 bits 0101100 = 0x2C with continuation, then 0x02.
  auto buf{std::array<std::byte, 8>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_varint(300).has_value());
  auto const out{w.written()};
  REQUIRE(out.size() == 2);
  CHECK(out[0] == std::byte{0xAC});
  CHECK(out[1] == std::byte{0x02});
}

TEST_CASE("nexenne::serialization::binary - varint u64 max decodes to all-ones") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_varint(std::numeric_limits<std::uint64_t>::max()).has_value());
  CHECK(w.bytes_written() == 10);
  auto r{binary::reader{w.written()}};
  CHECK(*r.read_varint() == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("nexenne::serialization::binary - varint rejects 11-byte over-long encoding") {
  auto raw{std::array<std::byte, 11>{}};
  for (auto& b : raw)
    b = std::byte{0xFF};
  raw[10] = std::byte{0x7F};
  auto r{binary::reader{std::span<std::byte const>{raw}}};
  auto const got{r.read_varint()};
  CHECK(!got.has_value());
  CHECK(got.error() == error::invalid_input);
}

TEST_CASE("nexenne::serialization::binary - varint rejects over-wide 10th byte") {
  auto raw{std::array<std::byte, 10>{}};
  for (auto i{std::size_t{0}}; i < 9; ++i)
    raw[i] = std::byte{0x80};
  raw[9] = std::byte{0x02};
  auto r{binary::reader{std::span<std::byte const>{raw}}};
  auto const got{r.read_varint()};
  CHECK(!got.has_value());
  CHECK(got.error() == error::invalid_input);
}

TEST_CASE("nexenne::serialization::binary - varint 10th byte value 0x01 is accepted") {
  auto raw{std::array<std::byte, 10>{}};
  for (auto i{std::size_t{0}}; i < 9; ++i)
    raw[i] = std::byte{0x80};
  raw[9] = std::byte{0x01};
  auto r{binary::reader{std::span<std::byte const>{raw}}};
  auto const got{r.read_varint()};
  REQUIRE(got.has_value());
  CHECK(*got == (std::uint64_t{1} << 63));
}

TEST_CASE("nexenne::serialization::binary - zigzag signed values round trip") {
  auto const cases{std::array<std::int64_t, 13>{
    0,
    -1,
    1,
    63,
    -64,
    64,
    -65,
    std::int64_t{-1'000'000'000},
    std::int64_t{1'000'000'000},
    std::numeric_limits<std::int64_t>::max(),
    std::numeric_limits<std::int64_t>::min(),
    std::numeric_limits<std::int64_t>::max() - 1,
    std::numeric_limits<std::int64_t>::min() + 1,
  }};
  for (auto const value : cases) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{binary::writer{buf}};
    REQUIRE(w.write_zigzag(value).has_value());
    auto r{binary::reader{w.written()}};
    auto const got{r.read_zigzag()};
    REQUIRE(got.has_value());
    CHECK(*got == value);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::binary - zigzag small magnitudes are one byte") {
  for (auto v{std::int64_t{-63}}; v <= 63; ++v) {
    auto buf{std::array<std::byte, 8>{}};
    auto w{binary::writer{buf}};
    REQUIRE(w.write_zigzag(v).has_value());
    CHECK(w.bytes_written() == 1);
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{binary::writer{buf}};
    REQUIRE(w.write_zigzag(-64).has_value());
    CHECK(w.bytes_written() == 1);
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{binary::writer{buf}};
    REQUIRE(w.write_zigzag(64).has_value());
    CHECK(w.bytes_written() == 2);
  }
}

TEST_CASE("nexenne::serialization::binary - zigzag int64 min does not invoke signed UB") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_zigzag(std::numeric_limits<std::int64_t>::min()).has_value());
  CHECK(w.bytes_written() == 10);
  auto r{binary::reader{w.written()}};
  CHECK(*r.read_zigzag() == std::numeric_limits<std::int64_t>::min());
}

TEST_CASE("nexenne::serialization::binary - zigzag propagates varint decode errors") {
  auto raw{std::array<std::byte, 11>{}};
  for (auto& b : raw)
    b = std::byte{0xFF};
  raw[10] = std::byte{0x7F};
  auto r{binary::reader{std::span<std::byte const>{raw}}};
  auto const got{r.read_zigzag()};
  CHECK(!got.has_value());
  CHECK(got.error() == error::invalid_input);
}

TEST_CASE("nexenne::serialization::binary - array bulk read/write u32") {
  auto const src{std::array<std::uint32_t, 4>{0xAA, 0xBB, 0xCC, 0xDD}};
  auto buf{std::array<std::byte, 64>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_array(std::span<std::uint32_t const>{src}).has_value());
  CHECK(w.bytes_written() == 16);

  auto dst{std::array<std::uint32_t, 4>{}};
  auto r{binary::reader{w.written()}};
  REQUIRE(r.read_array(std::span<std::uint32_t>{dst}).has_value());
  CHECK(dst == src);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - array of doubles round trips bit-exact") {
  auto const src{std::array<double, 3>{
    std::numeric_limits<double>::infinity(),
    -0.0,
    1.25,
  }};
  auto buf{std::array<std::byte, 64>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_array(std::span<double const>{src}).has_value());
  CHECK(w.bytes_written() == 24);

  auto dst{std::array<double, 3>{}};
  auto r{binary::reader{w.written()}};
  REQUIRE(r.read_array(std::span<double>{dst}).has_value());
  for (auto i{std::size_t{0}}; i < 3; ++i)
    CHECK(bits_equal(dst[i], src[i]));
}

TEST_CASE("nexenne::serialization::binary - empty array is a no-op success") {
  auto buf{std::array<std::byte, 8>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_array(std::span<std::uint32_t const>{}).has_value());
  CHECK(w.bytes_written() == 0);

  auto r{binary::reader{w.written()}};
  auto dst{std::span<std::uint32_t>{}};
  REQUIRE(r.read_array(dst).has_value());
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - array byte-of-1 element fast path") {
  auto const src{std::array<std::uint8_t, 4>{1, 2, 3, 4}};
  auto buf{std::array<std::byte, 8>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_array(std::span<std::uint8_t const>{src}).has_value());
  CHECK(w.bytes_written() == 4);

  auto dst{std::array<std::uint8_t, 4>{}};
  auto r{binary::reader{w.written()}};
  REQUIRE(r.read_array(std::span<std::uint8_t>{dst}).has_value());
  CHECK(dst == src);
}

TEST_CASE("nexenne::serialization::binary - peek does not advance and matches read") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint16_t{0xBEEF}).has_value());
  REQUIRE(w.write(std::uint8_t{0x7F}).has_value());

  auto r{binary::reader{w.written()}};
  CHECK(*r.peek<std::uint16_t>() == 0xBEEF);
  CHECK(r.position() == 0);
  CHECK(*r.read<std::uint16_t>() == 0xBEEF);
  CHECK(r.position() == 2);
  CHECK(*r.peek<std::uint8_t>() == 0x7F);
  CHECK(r.position() == 2);
}

TEST_CASE("nexenne::serialization::binary - skip / seek navigate the buffer") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{0x11223344}).has_value());
  REQUIRE(w.write(std::uint16_t{0x5566}).has_value());

  auto r{binary::reader{w.written()}};
  REQUIRE(r.skip(4).has_value());
  CHECK(*r.read<std::uint16_t>() == 0x5566);
  REQUIRE(r.seek(0).has_value());
  CHECK(*r.read<std::uint32_t>() == 0x11223344);
  REQUIRE(r.seek(r.capacity()).has_value());
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - seek past end rejected, cursor unchanged") {
  auto buf{std::array<std::byte, 4>{}};
  auto r{binary::reader{std::span<std::byte const>{buf}}};
  REQUIRE(r.skip(2).has_value());
  auto const bad{r.seek(5)};
  CHECK(!bad.has_value());
  CHECK(bad.error() == error::buffer_underrun);
  CHECK(r.position() == 2);
}

TEST_CASE("nexenne::serialization::binary - writer reset and skip backfill") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{42}).has_value());
  CHECK(w.bytes_written() == 4);
  w.reset();
  CHECK(w.bytes_written() == 0);
  CHECK(w.bytes_remaining() == 16);
  REQUIRE(w.skip(2).has_value());
  CHECK(w.bytes_written() == 2);
  REQUIRE(w.write(std::uint64_t{100}).has_value());
  CHECK(w.bytes_written() == 10);
}

TEST_CASE("nexenne::serialization::binary - truncated primitive read fails cleanly") {
  auto full{std::array<std::byte, 4>{}};
  {
    auto w{binary::writer{full}};
    REQUIRE(w.write(std::uint32_t{0xCAFEBABE}).has_value());
  }
  for (auto len{std::size_t{0}}; len < 4; ++len) {
    auto r{binary::reader{std::span<std::byte const>{full.data(), len}}};
    auto const got{r.read<std::uint32_t>()};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_underrun);
    CHECK(r.position() == 0);
  }
}

TEST_CASE("nexenne::serialization::binary - every prefix of a full message fails or stops") {
  auto buf{std::array<std::byte, 64>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{0xABCDEF01}).has_value());
  REQUIRE(w.write(2.5).has_value());
  REQUIRE(w.write(std::string_view{"sensor"}).has_value());
  auto const full_len{w.bytes_written()};

  for (auto len{std::size_t{0}}; len < full_len; ++len) {
    auto r{binary::reader{std::span<std::byte const>{buf.data(), len}}};
    auto ok{true};
    if (auto const a{r.read<std::uint32_t>()}; !a)
      ok = false;
    if (ok)
      if (auto const b{r.read<double>()}; !b)
        ok = false;
    if (ok)
      if (auto const c{r.read_string()}; !c)
        ok = false;
    CHECK_FALSE(ok);
    CHECK(r.position() <= len);
  }

  auto rf{binary::reader{std::span<std::byte const>{buf.data(), full_len}}};
  CHECK(*rf.read<std::uint32_t>() == 0xABCDEF01u);
  CHECK(*rf.read<double>() == 2.5);
  CHECK(*rf.read_string() == "sensor");
  CHECK(rf.at_end());
}

TEST_CASE("nexenne::serialization::binary - string with body shorter than prefix claims") {
  auto const raw{std::array<std::byte, 4>{
    std::byte{0x0A},
    std::byte{'a'},
    std::byte{'b'},
    std::byte{'c'},
  }};
  auto r{binary::reader{std::span<std::byte const>{raw}}};
  auto const got{r.read_string()};
  CHECK(!got.has_value());
  CHECK(got.error() == error::buffer_underrun);
}

TEST_CASE("nexenne::serialization::binary - huge string prefix rejected, no overflow") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_varint(0xFFFFFFFFFFFFFFFFull).has_value());
  auto const prefix{w.written()};

  SUBCASE("default cap rejects with string_too_long") {
    auto r{binary::reader{prefix}};
    auto const got{r.read_string()};
    CHECK(!got.has_value());
    CHECK(got.error() == error::string_too_long);
  }
  SUBCASE("cap lifted still rejects with buffer_underrun") {
    auto r{binary::reader{prefix}};
    r.max_string_size() = std::numeric_limits<std::size_t>::max();
    auto const got{r.read_string()};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::binary - truncated varint mid-stream fails cleanly") {
  for (auto len{std::size_t{1}}; len <= 3; ++len) {
    auto raw{std::vector<std::byte>(len, std::byte{0x80})};
    auto r{binary::reader{std::span<std::byte const>{raw.data(), raw.size()}}};
    auto const got{r.read_varint()};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::binary - read_bytes / skip past end fail cleanly") {
  auto const raw{std::array<std::byte, 3>{std::byte{1}, std::byte{2}, std::byte{3}}};
  auto r{binary::reader{std::span<std::byte const>{raw}}};
  auto const got{r.read_bytes(4)};
  CHECK(!got.has_value());
  CHECK(got.error() == error::buffer_underrun);
  CHECK(r.position() == 0);

  auto const sk{r.skip(4)};
  CHECK(!sk.has_value());
  CHECK(sk.error() == error::buffer_underrun);
  CHECK(r.position() == 0);
}

TEST_CASE("nexenne::serialization::binary - read_array rejects an over-long request") {
  auto buf{std::array<std::byte, 8>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{1}).has_value());
  REQUIRE(w.write(std::uint32_t{2}).has_value());

  auto r{binary::reader{w.written()}};
  auto dst{std::array<std::uint32_t, 3>{}};
  auto const got{r.read_array(std::span<std::uint32_t>{dst})};
  CHECK(!got.has_value());
  CHECK(got.error() == error::buffer_underrun);
  CHECK(r.position() == 0);
}

TEST_CASE("nexenne::serialization::binary - peek on a short buffer fails, cursor still 0") {
  auto buf{std::array<std::byte, 2>{}};
  auto r{binary::reader{std::span<std::byte const>{buf}}};
  auto const pk{r.peek<std::uint32_t>()};
  CHECK(!pk.has_value());
  CHECK(pk.error() == error::buffer_underrun);
  CHECK(r.position() == 0);
}

TEST_CASE("nexenne::serialization::binary - writer reports buffer_full per operation") {
  SUBCASE("primitive into too-small buffer") {
    auto buf{std::array<std::byte, 3>{}};
    auto w{binary::writer{buf}};
    auto const got{w.write(std::uint32_t{1})};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("write_bytes into too-small buffer") {
    auto buf{std::array<std::byte, 2>{}};
    auto const payload{std::array<std::byte, 3>{std::byte{1}, std::byte{2}, std::byte{3}}};
    auto w{binary::writer{buf}};
    auto const got{w.write_bytes(std::span<std::byte const>{payload})};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("write_array into too-small buffer") {
    auto buf{std::array<std::byte, 7>{}};
    auto const src{std::array<std::uint32_t, 2>{1, 2}};
    auto w{binary::writer{buf}};
    auto const got{w.write_array(std::span<std::uint32_t const>{src})};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("write_varint into too-small buffer") {
    auto buf{std::array<std::byte, 1>{}};
    auto w{binary::writer{buf}};
    auto const got{w.write_varint(128)};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("write string prefix+body does not fit") {
    auto buf{std::array<std::byte, 3>{}};
    auto w{binary::writer{buf}};
    auto const got{w.write(std::string_view{"abc"})};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("skip past end") {
    auto buf{std::array<std::byte, 2>{}};
    auto w{binary::writer{buf}};
    auto const got{w.skip(3)};
    CHECK(!got.has_value());
    CHECK(got.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
}

TEST_CASE("nexenne::serialization::binary - writer fills buffer to the exact byte") {
  auto buf{std::array<std::byte, 4>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{0x01020304}).has_value());
  CHECK(w.bytes_written() == 4);
  CHECK(w.bytes_remaining() == 0);
  auto const got{w.write(std::uint8_t{0})};
  CHECK(!got.has_value());
  CHECK(got.error() == error::buffer_full);
  CHECK(w.bytes_written() == 4);
}

TEST_CASE("nexenne::serialization::binary - varint that fits to the last byte succeeds") {
  auto buf{std::array<std::byte, 2>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_varint(128).has_value());
  CHECK(w.bytes_written() == 2);
  CHECK(w.bytes_remaining() == 0);
}

TEST_CASE("nexenne::serialization::binary - string into a zero-size buffer fails") {
  auto buf{std::array<std::byte, 0>{}};
  auto w{binary::writer{std::span<std::byte>{buf}}};
  auto const got{w.write(std::string_view{""})};
  CHECK(!got.has_value());
  CHECK(got.error() == error::buffer_full);
}

TEST_CASE("nexenne::serialization::binary - empty string still emits a one-byte prefix") {
  auto buf{std::array<std::byte, 1>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::string_view{""}).has_value());
  CHECK(w.bytes_written() == 1);
  auto const out{w.written()};
  CHECK(out[0] == std::byte{0x00});
  auto r{binary::reader{out}};
  CHECK(*r.read_string() == "");
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - reachable error codes from the codec") {
  {
    auto buf{std::array<std::byte, 1>{}};
    auto w{binary::writer{buf}};
    CHECK(w.write(std::uint32_t{0}).error() == error::buffer_full);
  }
  {
    auto buf{std::array<std::byte, 1>{}};
    auto r{binary::reader{std::span<std::byte const>{buf}}};
    CHECK(r.read<std::uint32_t>().error() == error::buffer_underrun);
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{binary::writer{buf}};
    REQUIRE(w.write(std::string_view{"abcdef"}).has_value());
    auto r{binary::reader{w.written()}};
    CHECK(r.max_string_size() == binary::reader::default_max_string_size);
    r.max_string_size() = 3;
    CHECK(r.max_string_size() == 3);
    CHECK(r.read_string().error() == error::string_too_long);
  }
  {
    auto raw{std::array<std::byte, 11>{}};
    for (auto& b : raw)
      b = std::byte{0xFF};
    auto r{binary::reader{std::span<std::byte const>{raw}}};
    CHECK(r.read_varint().error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::binary - to_string names every error enumerator") {
  CHECK(to_string(error::invalid_input) == "invalid_input");
  CHECK(to_string(error::unexpected_character) == "unexpected_character");
  CHECK(to_string(error::unexpected_end) == "unexpected_end");
  CHECK(to_string(error::invalid_number) == "invalid_number");
  CHECK(to_string(error::invalid_string) == "invalid_string");
  CHECK(to_string(error::invalid_escape) == "invalid_escape");
  CHECK(to_string(error::duplicate_key) == "duplicate_key");
  CHECK(to_string(error::depth_limit_exceeded) == "depth_limit_exceeded");
  CHECK(to_string(error::type_mismatch) == "type_mismatch");
  CHECK(to_string(error::path_not_found) == "path_not_found");
  CHECK(to_string(error::buffer_full) == "buffer_full");
  CHECK(to_string(error::buffer_underrun) == "buffer_underrun");
  CHECK(to_string(error::string_too_long) == "string_too_long");
  CHECK(to_string(static_cast<error>(0xFF)) == "?");
}

TEST_CASE("nexenne::serialization::binary - mixed schema round trip end to end") {
  auto buf{std::array<std::byte, 256>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{0xDEADBEEF}).has_value());
  REQUIRE(w.write(std::int64_t{-42}).has_value());
  REQUIRE(w.write(3.14).has_value());
  REQUIRE(w.write_varint(300).has_value());
  REQUIRE(w.write_zigzag(-7).has_value());
  REQUIRE(w.write(std::string_view{"hello"}).has_value());
  auto const payload{std::array<std::byte, 2>{std::byte{0xAA}, std::byte{0xBB}}};
  REQUIRE(w.write_bytes(std::span<std::byte const>{payload}).has_value());

  auto r{binary::reader{w.written()}};
  CHECK(*r.read<std::uint32_t>() == 0xDEADBEEF);
  CHECK(*r.read<std::int64_t>() == -42);
  CHECK(*r.read<double>() == 3.14);
  CHECK(*r.read_varint() == 300u);
  CHECK(*r.read_zigzag() == -7);
  CHECK(*r.read_string() == "hello");
  auto const tail{r.read_bytes(2)};
  REQUIRE(tail.has_value());
  CHECK((*tail)[0] == std::byte{0xAA});
  CHECK((*tail)[1] == std::byte{0xBB});
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - read<bool> accepts 0 and 1") {
  auto const buf{std::array<std::byte, 2>{std::byte{0x00}, std::byte{0x01}}};
  auto r{binary::reader{std::span<std::byte const>{buf}}};
  CHECK(*r.read<bool>() == false);
  CHECK(*r.read<bool>() == true);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::binary - read<bool> rejects a byte outside {0, 1}") {
  auto const buf{std::array<std::byte, 1>{std::byte{0x02}}};
  auto r{binary::reader{std::span<std::byte const>{buf}}};

  SUBCASE("read<bool>") {
    auto const v{r.read<bool>()};
    REQUIRE_FALSE(v.has_value());
    CHECK(v.error() == error::invalid_input);
    CHECK(r.position() == 0);
  }
  SUBCASE("peek<bool>") {
    auto const v{r.peek<bool>()};
    REQUIRE_FALSE(v.has_value());
    CHECK(v.error() == error::invalid_input);
    CHECK(r.position() == 0);
  }
}

TEST_CASE("nexenne::serialization::binary - read_array<bool> validates every wire byte") {
  SUBCASE("all valid") {
    auto const buf{std::array<std::byte, 3>{std::byte{0x01}, std::byte{0x00}, std::byte{0x01}}};
    auto r{binary::reader{std::span<std::byte const>{buf}}};
    auto out{std::array<bool, 3>{}};
    REQUIRE(r.read_array(std::span<bool>{out}).has_value());
    CHECK(out[0] == true);
    CHECK(out[1] == false);
    CHECK(out[2] == true);
  }
  SUBCASE("a byte outside {0, 1} is rejected and leaves the cursor put") {
    auto const buf{std::array<std::byte, 3>{std::byte{0x01}, std::byte{0x02}, std::byte{0x00}}};
    auto r{binary::reader{std::span<std::byte const>{buf}}};
    auto out{std::array<bool, 3>{}};
    auto const v{r.read_array(std::span<bool>{out})};
    REQUIRE_FALSE(v.has_value());
    CHECK(v.error() == error::invalid_input);
    CHECK(r.position() == 0);
  }
}

template <typename T>
concept writes = requires(binary::writer& w, T const t) { w.write(t); };

template <typename T>
concept writes_array =
  requires(binary::writer& w, std::span<T const> const xs) { w.write_array(xs); };

template <typename T>
concept reads = requires(binary::reader& r) { r.template read<T>(); };

template <typename T>
concept peeks = requires(binary::reader const& r) { r.template peek<T>(); };

template <typename T>
concept reads_array = requires(binary::reader& r, std::span<T> const out) { r.read_array(out); };

template <typename T>
concept wire_scalar = writes<T> && writes_array<T> && reads<T> && peeks<T> && reads_array<T>;

template <typename T>
concept no_wire_scalar =
  !writes<T> && !writes_array<T> && !reads<T> && !peeks<T> && !reads_array<T>;

static_assert(wire_scalar<bool>);
static_assert(wire_scalar<char>);
static_assert(wire_scalar<char8_t>);
static_assert(wire_scalar<std::int8_t> && wire_scalar<std::uint8_t>);
static_assert(wire_scalar<std::int16_t> && wire_scalar<std::uint16_t>);
static_assert(wire_scalar<std::int32_t> && wire_scalar<std::uint32_t>);
static_assert(wire_scalar<std::int64_t> && wire_scalar<std::uint64_t>);
static_assert(wire_scalar<float> && wire_scalar<double>);
static_assert(reads<std::uint32_t const>);

static_assert(no_wire_scalar<wchar_t>);
static_assert(no_wire_scalar<char16_t>);
static_assert(no_wire_scalar<char32_t>);
static_assert(no_wire_scalar<long double>);
// By type identity, not width: where std::int64_t is long, long long is rejected.
static_assert(!std::same_as<std::int64_t, long> || no_wire_scalar<long long>);
static_assert(!std::same_as<std::uint64_t, unsigned long> || no_wire_scalar<unsigned long long>);
static_assert(!std::same_as<std::int32_t, long> || no_wire_scalar<int>);

}  // namespace
