#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/serialization/serialization.hpp>

namespace {

using namespace nexenne::serialization;

// Build an immutable byte span over an array literal of unsigned char values.
template <typename... Bs>
[[nodiscard]] auto bytes_of(Bs... bs) -> std::vector<std::byte> {
  return {static_cast<std::byte>(static_cast<std::uint8_t>(bs))...};
}

// Compare a writer's emitted prefix against an expected byte sequence.
[[nodiscard]] auto
written_equals(cbor::writer const& w, std::span<std::uint8_t const> const expected) -> bool {
  auto const out{w.written()};
  if (out.size() != expected.size())
    return false;
  for (std::size_t i{0}; i < out.size(); ++i) {
    if (static_cast<std::uint8_t>(out[i]) != expected[i])
      return false;
  }
  return true;
}

TEST_CASE("nexenne::serialization::cbor round-trips every major type") {
  auto buf{std::array<std::byte, 256>{}};
  auto w{cbor::writer{buf}};
  auto const payload{
    std::array<std::byte, 4>{std::byte{0x00}, std::byte{0x7F}, std::byte{0x80}, std::byte{0xFF}}
  };
  REQUIRE(w.write_uint(1000000).has_value());
  REQUIRE(w.write_int(-44).has_value());
  REQUIRE(w.write_bytes(std::span<std::byte const>{payload}).has_value());
  REQUIRE(w.write_string("hello").has_value());
  REQUIRE(w.write_array_header(2).has_value());
  REQUIRE(w.write_uint(1).has_value());
  REQUIRE(w.write_uint(2).has_value());
  REQUIRE(w.write_map_header(1).has_value());
  REQUIRE(w.write_string("k").has_value());
  REQUIRE(w.write_uint(9).has_value());
  REQUIRE(w.write_bool(true).has_value());
  REQUIRE(w.write_bool(false).has_value());
  REQUIRE(w.write_null().has_value());
  REQUIRE(w.write_float32(3.5F).has_value());
  REQUIRE(w.write_float64(2.718281828459045).has_value());
  // `undefined` is written LAST: the reader can identify it via peek_type but
  // has no consuming reader for it (read_null exists, read_undefined does not),
  // so nothing may follow it in a sequential round-trip.
  REQUIRE(w.write_undefined().has_value());

  auto r{cbor::reader{w.written()}};
  CHECK(*r.read_uint() == 1000000u);
  CHECK(*r.read_int() == -44);
  auto const bs{r.read_bytes()};
  REQUIRE(bs.has_value());
  REQUIRE(bs->size() == 4);
  CHECK((*bs)[0] == std::byte{0x00});
  CHECK((*bs)[3] == std::byte{0xFF});
  CHECK(*r.read_string() == "hello");
  CHECK(*r.read_array_header() == 2u);
  CHECK(*r.read_uint() == 1u);
  CHECK(*r.read_uint() == 2u);
  CHECK(*r.read_map_header() == 1u);
  CHECK(*r.read_string() == "k");
  CHECK(*r.read_uint() == 9u);
  CHECK(*r.read_bool() == true);
  CHECK(*r.read_bool() == false);
  REQUIRE(r.read_null().has_value());
  CHECK(*r.read_float() == doctest::Approx(3.5));
  CHECK(*r.read_float() == doctest::Approx(2.718281828459045));
  // `undefined` was written last; the reader can identify it but not consume it.
  CHECK(*r.peek_type() == cbor::type::undefined);
  CHECK(!r.at_end());
}

TEST_CASE("nexenne::serialization::cbor matches RFC 8949 Appendix A unsigned vectors") {
  struct vec {
    std::uint64_t value;
    std::vector<std::uint8_t> bytes;
  };

  auto const cases{std::vector<vec>{
    {0, {0x00}},
    {1, {0x01}},
    {10, {0x0a}},
    {23, {0x17}},
    {24, {0x18, 0x18}},
    {25, {0x18, 0x19}},
    {100, {0x18, 0x64}},
    {1000, {0x19, 0x03, 0xe8}},
    {1000000, {0x1a, 0x00, 0x0f, 0x42, 0x40}},
    {1000000000000ULL, {0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00}},
    {18446744073709551615ULL, {0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
  }};
  for (auto const& c : cases) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_uint(c.value).has_value());
    CHECK(written_equals(w, c.bytes));

    auto r{cbor::reader{w.written()}};
    CHECK(*r.peek_type() == cbor::type::unsigned_int);
    CHECK(*r.read_uint() == c.value);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::cbor matches RFC 8949 Appendix A negative vectors") {
  struct vec {
    std::int64_t value;
    std::vector<std::uint8_t> bytes;
  };

  auto const cases{std::vector<vec>{
    {-1, {0x20}},
    {-10, {0x29}},
    {-100, {0x38, 0x63}},
    {-1000, {0x39, 0x03, 0xe7}},
    {-24, {0x37}},
    {-25, {0x38, 0x18}},
  }};
  for (auto const& c : cases) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_int(c.value).has_value());
    CHECK(written_equals(w, c.bytes));

    auto r{cbor::reader{w.written()}};
    CHECK(*r.peek_type() == cbor::type::negative_int);
    CHECK(*r.read_int() == c.value);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::cbor matches RFC 8949 Appendix A string vectors") {
  {  // empty byte string -> 0x40
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_bytes(std::span<std::byte const>{}).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x40}));
  }
  {  // h'01020304' -> 0x4401020304
    auto buf{std::array<std::byte, 8>{}};
    auto const body{
      std::array<std::byte, 4>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}
    };
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_bytes(std::span<std::byte const>{body}).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x44, 0x01, 0x02, 0x03, 0x04}));
  }
  {  // "" -> 0x60
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_string("").has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x60}));
  }
  {  // "a" -> 0x6161
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_string("a").has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x61, 0x61}));
  }
  {  // "IETF" -> 0x6449455446
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_string("IETF").has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x64, 0x49, 0x45, 0x54, 0x46}));
  }
  {  // "\"\\" -> 0x62225c
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_string("\"\\").has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x62, 0x22, 0x5c}));
  }
}

TEST_CASE("nexenne::serialization::cbor matches RFC 8949 Appendix A container vectors") {
  {  // [] -> 0x80
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_array_header(0).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x80}));
  }
  {  // [1, 2, 3] -> 0x83010203
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_array_header(3).has_value());
    REQUIRE(w.write_uint(1).has_value());
    REQUIRE(w.write_uint(2).has_value());
    REQUIRE(w.write_uint(3).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x83, 0x01, 0x02, 0x03}));
  }
  {  // {} -> 0xa0
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_map_header(0).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0xa0}));
  }
  {  // {1: 2, 3: 4} -> 0xa201020304
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_map_header(2).has_value());
    REQUIRE(w.write_uint(1).has_value());
    REQUIRE(w.write_uint(2).has_value());
    REQUIRE(w.write_uint(3).has_value());
    REQUIRE(w.write_uint(4).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0xa2, 0x01, 0x02, 0x03, 0x04}));
  }
  {  // {"a": 1, "b": [2, 3]} -> 0xa26161016162820203
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_map_header(2).has_value());
    REQUIRE(w.write_string("a").has_value());
    REQUIRE(w.write_uint(1).has_value());
    REQUIRE(w.write_string("b").has_value());
    REQUIRE(w.write_array_header(2).has_value());
    REQUIRE(w.write_uint(2).has_value());
    REQUIRE(w.write_uint(3).has_value());
    CHECK(written_equals(
      w, std::vector<std::uint8_t>{0xa2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0x02, 0x03}
    ));
  }
}

TEST_CASE("nexenne::serialization::cbor matches RFC 8949 Appendix A simple-value vectors") {
  auto check_one{[](auto write_fn, std::vector<std::uint8_t> const& expected) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(write_fn(w).has_value());
    CHECK(written_equals(w, expected));
  }};
  check_one([](cbor::writer& w) { return w.write_bool(false); }, {0xf4});
  check_one([](cbor::writer& w) { return w.write_bool(true); }, {0xf5});
  check_one([](cbor::writer& w) { return w.write_null(); }, {0xf6});
  check_one([](cbor::writer& w) { return w.write_undefined(); }, {0xf7});
}

TEST_CASE("nexenne::serialization::cbor decodes RFC 8949 Appendix A float vectors") {
  struct vec {
    std::vector<std::uint8_t> bytes;
    double expected;
  };

  auto const cases{std::vector<vec>{
    {{0xf9, 0x00, 0x00}, 0.0},      // half 0.0
    {{0xf9, 0x3c, 0x00}, 1.0},      // half 1.0
    {{0xf9, 0x3e, 0x00}, 1.5},      // half 1.5
    {{0xf9, 0x7b, 0xff}, 65504.0},  // half max normal
    {{0xfa, 0x47, 0xc3, 0x50, 0x00}, 100000.0},
    {{0xfa, 0x7f, 0x7f, 0xff, 0xff}, 3.4028234663852886e+38},
    {{0xfb, 0x3f, 0xf1, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a}, 1.1},
    {{0xfb, 0x40, 0x09, 0x21, 0xfb, 0x54, 0x44, 0x2d, 0x18}, 3.141592653589793},
    {{0xf9, 0x00, 0x01}, 5.960464477539063e-08},  // smallest subnormal half
    {{0xf9, 0x04, 0x00}, 6.103515625e-05},        // smallest normal half
    {{0xf9, 0xc4, 0x00}, -4.0},                   // negative half
  }};
  for (auto const& c : cases) {
    std::vector<std::byte> raw;
    for (auto const b : c.bytes)
      raw.push_back(static_cast<std::byte>(b));
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    CHECK(*r.peek_type() == cbor::type::floating);
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(*d == doctest::Approx(c.expected));
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::cbor decodes half-float infinities and NaN") {
  {  // +inf 0xf97c00
    auto const raw{bytes_of(0xf9, 0x7c, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isinf(*d));
    CHECK(*d > 0.0);
  }
  {  // -inf 0xf9fc00
    auto const raw{bytes_of(0xf9, 0xfc, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isinf(*d));
    CHECK(*d < 0.0);
  }
  {  // NaN 0xf97e00
    auto const raw{bytes_of(0xf9, 0x7e, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isnan(*d));
  }
  {  // single +inf 0xfa7f800000
    auto const raw{bytes_of(0xfa, 0x7f, 0x80, 0x00, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isinf(*d));
  }
  {  // double NaN 0xfb7ff8000000000000
    auto const raw{bytes_of(0xfb, 0x7f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isnan(*d));
  }
}

TEST_CASE("nexenne::serialization::cbor encodes unsigned integers at every length boundary") {
  struct vec {
    std::uint64_t value;
    std::size_t encoded_size;
    std::uint8_t head;
  };

  auto const cases{std::vector<vec>{
    {23, 1, 0x17},                       // largest single-byte form
    {24, 2, 0x18},                       // first 1-byte-argument form
    {255, 2, 0x18},                      // largest 1-byte-argument form
    {256, 3, 0x19},                      // first 2-byte form
    {65535, 3, 0x19},                    // largest 2-byte form
    {65536, 5, 0x1a},                    // first 4-byte form
    {4294967295ULL, 5, 0x1a},            // largest 4-byte form
    {4294967296ULL, 9, 0x1b},            // first 8-byte form
    {18446744073709551615ULL, 9, 0x1b},  // max u64
  }};
  for (auto const& c : cases) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_uint(c.value).has_value());
    CHECK(w.bytes_written() == c.encoded_size);
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == c.head);

    auto r{cbor::reader{w.written()}};
    CHECK(*r.read_uint() == c.value);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::cbor encodes negative integers at every length boundary") {
  struct vec {
    std::int64_t value;
    std::size_t encoded_size;
    std::uint8_t head;
  };

  auto const cases{std::vector<vec>{
    {-24, 1, 0x37},            // largest single-byte negative
    {-25, 2, 0x38},            // first 1-byte-argument negative
    {-256, 2, 0x38},           // -1-255 -> arg 255 still 1 byte
    {-257, 3, 0x39},           // arg 256 -> 2 bytes
    {-65536, 3, 0x39},         // arg 65535
    {-65537, 5, 0x3a},         // arg 65536 -> 4 bytes
    {-4294967296LL, 5, 0x3a},  // arg 0xFFFFFFFF
    {-4294967297LL, 9, 0x3b},  // arg 0x100000000 -> 8 bytes
  }};
  for (auto const& c : cases) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_int(c.value).has_value());
    CHECK(w.bytes_written() == c.encoded_size);
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == c.head);

    auto r{cbor::reader{w.written()}};
    CHECK(*r.read_int() == c.value);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::cbor reads int64 extremes without overflow") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_int(std::numeric_limits<std::int64_t>::max()).has_value());
  REQUIRE(w.write_int(std::numeric_limits<std::int64_t>::min()).has_value());

  auto r{cbor::reader{w.written()}};
  CHECK(*r.read_int() == std::numeric_limits<std::int64_t>::max());
  CHECK(*r.read_int() == std::numeric_limits<std::int64_t>::min());
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::cbor read_int rejects args overflowing int64") {
  {  // unsigned 2^63 (one past int64 max) -> type_mismatch
    auto const raw{bytes_of(0x1b, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const v{r.read_int()};
    CHECK(!v.has_value());
    CHECK(v.error() == error::type_mismatch);
  }
  {  // negative with arg 2^63 (encodes -2^63-1, below int64 min) -> type_mismatch
    auto const raw{bytes_of(0x3b, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const v{r.read_int()};
    CHECK(!v.has_value());
    CHECK(v.error() == error::type_mismatch);
  }
  {  // read_uint still accepts the full u64 range
    auto const raw{bytes_of(0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    CHECK(*r.read_uint() == 18446744073709551615ULL);
  }
}

TEST_CASE("nexenne::serialization::cbor decodes the smallest subnormal half exactly") {
  // f9 00 01 = 2^-24, the smallest positive subnormal half. The historical bug
  // decoded every subnormal to a quarter of its true value.
  auto const raw{bytes_of(0xf9, 0x00, 0x01)};
  auto r{cbor::reader{std::span<std::byte const>{raw}}};
  auto const d{r.read_float()};
  REQUIRE(d.has_value());
  CHECK(*d == 5.9604644775390625e-08);  // exact, not a quarter
}

TEST_CASE("nexenne::serialization::cbor decodes the largest subnormal half exactly") {
  // f9 03 ff = (1023/1024) * 2^-14, the largest subnormal half.
  auto const raw{bytes_of(0xf9, 0x03, 0xff)};
  auto r{cbor::reader{std::span<std::byte const>{raw}}};
  auto const d{r.read_float()};
  REQUIRE(d.has_value());
  CHECK(*d == doctest::Approx(6.097555160522461e-05));
}

TEST_CASE("nexenne::serialization::cbor decodes negative-zero and mid-range subnormal halves") {
  {  // f9 80 00 = -0.0
    auto const raw{bytes_of(0xf9, 0x80, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(*d == 0.0);
    CHECK(std::signbit(*d));
  }
  {  // f9 80 01 = -2^-24
    auto const raw{bytes_of(0xf9, 0x80, 0x01)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(*d == -5.9604644775390625e-08);
  }
  {  // f9 02 00 = (512/1024) * 2^-14 = 2^-15, a mid-range subnormal
    auto const raw{bytes_of(0xf9, 0x02, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(*d == doctest::Approx(3.0517578125e-05));
  }
}

TEST_CASE("nexenne::serialization::cbor round-trips a large double (historical bug)") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_float64(3.148e19).has_value());
  CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xFB);

  auto r{cbor::reader{w.written()}};
  CHECK(*r.read_float() == 3.148e19);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::cbor round-trips float32 and float64 values") {
  auto const f32_values{std::array<float, 5>{0.0F, -1.5F, 65504.0F, 3.4028235e38F, 1.0e-30F}};
  for (auto const v : f32_values) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_float32(v).has_value());
    auto r{cbor::reader{w.written()}};
    CHECK(*r.read_float() == doctest::Approx(static_cast<double>(v)));
  }
  auto const f64_values{
    std::array<double, 5>{0.0, -2.5, 1.7976931348623157e308, 2.2250738585072014e-308, 1234.5678}
  };
  for (auto const v : f64_values) {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_float64(v).has_value());
    auto r{cbor::reader{w.written()}};
    CHECK(*r.read_float() == doctest::Approx(v));
  }
}

TEST_CASE("nexenne::serialization::cbor rejects every truncated prefix of a valid stream") {
  // Encode a varied message, then feed back every strict prefix of it. No prefix
  // that cuts mid-item may read out of bounds; the reader must error cleanly.
  auto buf{std::array<std::byte, 256>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_uint(1000000).has_value());
  REQUIRE(w.write_int(-65537).has_value());
  REQUIRE(w.write_string("hello world").has_value());
  REQUIRE(w.write_array_header(2).has_value());
  REQUIRE(w.write_float64(3.14159).has_value());
  REQUIRE(w.write_float32(2.5F).has_value());
  auto const full{w.written()};

  for (std::size_t n{0}; n < full.size(); ++n) {
    auto const prefix{full.subspan(0, n)};
    auto r{cbor::reader{prefix}};
    // Drain the prefix with the matching reader calls; somewhere it must fail.
    auto failed{false};
    if (!r.read_uint().has_value())
      failed = true;
    else if (!r.read_int().has_value())
      failed = true;
    else if (!r.read_string().has_value())
      failed = true;
    else if (auto const a{r.read_array_header()}; !a.has_value())
      failed = true;
    else if (!r.read_float().has_value())
      failed = true;
    else if (!r.read_float().has_value())
      failed = true;
    CHECK(failed);  // every strict prefix is incomplete
  }
  // Sanity: the full stream decodes successfully.
  {
    auto r{cbor::reader{full}};
    CHECK(*r.read_uint() == 1000000u);
    CHECK(*r.read_int() == -65537);
    CHECK(*r.read_string() == "hello world");
    CHECK(*r.read_array_header() == 2u);
    CHECK(*r.read_float() == doctest::Approx(3.14159));
    CHECK(*r.read_float() == doctest::Approx(2.5));
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::cbor truncated integer arguments error cleanly") {
  // Heads claim a wide argument that the buffer does not contain.
  auto const cases{std::vector<std::vector<std::byte>>{
    bytes_of(0x18),                                // 1-byte arg head, no arg
    bytes_of(0x19, 0x01),                          // 2-byte arg, 1 present
    bytes_of(0x1a, 0x00, 0x0f, 0x42),              // 4-byte arg, 3 present
    bytes_of(0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4),  // 8-byte arg, 5 present
  }};
  for (auto const& raw : cases) {
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const v{r.read_uint()};
    CHECK(!v.has_value());
    CHECK(v.error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::cbor rejects a length claiming more than remains") {
  {  // text string header says 10 bytes, only 3 follow
    auto const raw{bytes_of(0x6a, 'a', 'b', 'c')};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const s{r.read_string()};
    CHECK(!s.has_value());
    CHECK(s.error() == error::buffer_underrun);
  }
  {  // byte string header says 8 bytes, only 2 follow
    auto const raw{bytes_of(0x48, 0x01, 0x02)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const b{r.read_bytes()};
    CHECK(!b.has_value());
    CHECK(b.error() == error::buffer_underrun);
  }
  {  // huge 8-byte length prefix on an empty body
    auto const raw{bytes_of(0x7b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const s{r.read_string()};
    CHECK(!s.has_value());
    CHECK(s.error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::cbor rejects reserved additional-info values") {
  // Additional info 28, 29, 30 are reserved; read_argument must reject them.
  for (auto const ai : {std::uint8_t{0x1c}, std::uint8_t{0x1d}, std::uint8_t{0x1e}}) {
    auto const raw{bytes_of(ai)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const v{r.read_uint()};
    CHECK(!v.has_value());
    CHECK(v.error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::cbor rejects the indefinite-length marker") {
  // Additional info 31 marks indefinite length, which this subset does not
  // support; read_argument treats it as invalid_input.
  {  // indefinite array header 0x9f
    auto const raw{bytes_of(0x9f)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const a{r.read_array_header()};
    CHECK(!a.has_value());
    CHECK(a.error() == error::invalid_input);
  }
  {  // indefinite text string 0x7f
    auto const raw{bytes_of(0x7f)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const s{r.read_string()};
    CHECK(!s.has_value());
    CHECK(s.error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::cbor rejects unsupported major type 6 (tags)") {
  // 0xc0..0xdf are major type 6. peek_type rejects, and a typed read mismatches.
  auto const raw{bytes_of(0xc0, 0x00)};
  auto r{cbor::reader{std::span<std::byte const>{raw}}};
  auto const t{r.peek_type()};
  CHECK(!t.has_value());
  CHECK(t.error() == error::invalid_input);
  auto const u{r.read_uint()};
  CHECK(!u.has_value());
  CHECK(u.error() == error::type_mismatch);
}

TEST_CASE("nexenne::serialization::cbor rejects unrecognised simple values via peek_type") {
  // Major 7 with additional info 0..19 or 24 are simple values the reader does
  // not classify; peek_type reports invalid_input.
  for (auto const b : {std::uint8_t{0xe0}, std::uint8_t{0xf0}, std::uint8_t{0xf8}}) {
    auto const raw{bytes_of(b)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const t{r.peek_type()};
    CHECK(!t.has_value());
    CHECK(t.error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::cbor truncated half/single/double floats error") {
  auto const cases{std::vector<std::vector<std::byte>>{
    bytes_of(0xf9, 0x3c),                    // half, 1 of 2 arg bytes
    bytes_of(0xfa, 0x47, 0xc3),              // single, 2 of 4 arg bytes
    bytes_of(0xfb, 0x3f, 0xf1, 0x99, 0x99),  // double, 4 of 8 arg bytes
    bytes_of(0xf9),                          // half head only
    bytes_of(0xfa),                          // single head only
    bytes_of(0xfb),                          // double head only
  }};
  for (auto const& raw : cases) {
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    CHECK(!d.has_value());
    CHECK(d.error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::cbor read calls on empty input report buffer_underrun") {
  auto const raw{std::vector<std::byte>{}};
  auto r{cbor::reader{std::span<std::byte const>{raw}}};
  CHECK(r.at_end());
  CHECK(r.read_uint().error() == error::buffer_underrun);
  CHECK(r.read_int().error() == error::buffer_underrun);
  CHECK(r.read_string().error() == error::buffer_underrun);
  CHECK(r.read_bytes().error() == error::buffer_underrun);
  CHECK(r.read_array_header().error() == error::buffer_underrun);
  CHECK(r.read_map_header().error() == error::buffer_underrun);
  CHECK(r.read_bool().error() == error::buffer_underrun);
  CHECK(r.read_float().error() == error::buffer_underrun);
  CHECK(r.peek_type().error() == error::buffer_underrun);
  // read_null reports type_mismatch at end of input per its contract.
  CHECK(r.read_null().error() == error::type_mismatch);
}

TEST_CASE("nexenne::serialization::cbor typed reads reject mismatched major types") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_uint(5).has_value());  // a single unsigned int

  {  // read_string on an unsigned int
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_string().error() == error::type_mismatch);
  }
  {  // read_bytes on an unsigned int
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_bytes().error() == error::type_mismatch);
  }
  {  // read_array_header on an unsigned int
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_array_header().error() == error::type_mismatch);
  }
  {  // read_map_header on an unsigned int
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_map_header().error() == error::type_mismatch);
  }
  {  // read_bool on an unsigned int
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_bool().error() == error::type_mismatch);
  }
  {  // read_float on an unsigned int
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_float().error() == error::type_mismatch);
  }
  {  // read_null on an unsigned int
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_null().error() == error::type_mismatch);
  }
}

TEST_CASE("nexenne::serialization::cbor writer reports buffer_full at each boundary") {
  {  // a single byte head does not fit in a zero-length buffer
    auto buf{std::array<std::byte, 0>{}};
    auto w{cbor::writer{buf}};
    auto const r{w.write_uint(0)};
    CHECK(!r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  {  // header fits but the body does not
    auto buf{std::array<std::byte, 2>{}};
    auto w{cbor::writer{buf}};
    auto const r{w.write_string("abcd")};  // needs 1 + 4 bytes
    CHECK(!r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  {  // 9-byte uint head will not fit in 8 bytes
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    auto const r{w.write_uint(18446744073709551615ULL)};
    CHECK(!r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  {  // float64 needs 9 bytes
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    auto const r{w.write_float64(1.0)};
    CHECK(!r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
}

TEST_CASE("nexenne::serialization::cbor writer reset reuses the buffer") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_uint(1000000).has_value());
  CHECK(w.bytes_written() == 5);
  w.reset();
  CHECK(w.bytes_written() == 0);
  REQUIRE(w.write_bool(true).has_value());
  CHECK(w.bytes_written() == 1);
  CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xf5);
}

TEST_CASE("nexenne::serialization::cbor round-trips empty and large strings") {
  auto buf{std::vector<std::byte>(70000, std::byte{0})};
  auto w{cbor::writer{buf}};
  auto const big{std::string(65540, 'q')};  // forces a 4-byte length header
  REQUIRE(w.write_string("").has_value());
  REQUIRE(w.write_string("x").has_value());
  REQUIRE(w.write_string(big).has_value());
  auto const empty_bytes{std::array<std::byte, 0>{}};
  REQUIRE(w.write_bytes(std::span<std::byte const>{empty_bytes}).has_value());

  auto r{cbor::reader{w.written()}};
  CHECK(*r.read_string() == "");
  CHECK(*r.read_string() == "x");
  auto const s{r.read_string()};
  REQUIRE(s.has_value());
  CHECK(s->size() == 65540);
  CHECK(*s == big);
  auto const eb{r.read_bytes()};
  REQUIRE(eb.has_value());
  CHECK(eb->empty());
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::cbor round-trips empty array and map headers") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_array_header(0).has_value());
  REQUIRE(w.write_map_header(0).has_value());

  auto r{cbor::reader{w.written()}};
  CHECK(*r.peek_type() == cbor::type::array_header);
  CHECK(*r.read_array_header() == 0u);
  CHECK(*r.peek_type() == cbor::type::map_header);
  CHECK(*r.read_map_header() == 0u);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::cbor round-trips deeply nested arrays") {
  constexpr int depth{200};
  auto buf{std::array<std::byte, 512>{}};
  auto w{cbor::writer{buf}};
  for (int i{0}; i < depth; ++i)
    REQUIRE(w.write_array_header(1).has_value());
  REQUIRE(w.write_uint(42).has_value());

  auto r{cbor::reader{w.written()}};
  for (int i{0}; i < depth; ++i)
    CHECK(*r.read_array_header() == 1u);
  CHECK(*r.read_uint() == 42u);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::cbor decodes a nested map-of-arrays document") {
  // Exact bytes for {"a": 1, "b": [2, 3]} decoded structurally.
  auto const raw{bytes_of(0xa2, 0x61, 0x61, 0x01, 0x61, 0x62, 0x82, 0x02, 0x03)};
  auto r{cbor::reader{std::span<std::byte const>{raw}}};
  REQUIRE(*r.read_map_header() == 2u);
  CHECK(*r.read_string() == "a");
  CHECK(*r.read_uint() == 1u);
  CHECK(*r.read_string() == "b");
  REQUIRE(*r.read_array_header() == 2u);
  CHECK(*r.read_uint() == 2u);
  CHECK(*r.read_uint() == 3u);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::cbor length prefixes past the size type are rejected") {
  // The helper is templated on the size type so the 32-bit narrowing path is
  // testable on a 64-bit host: a length above 4 GiB cannot be represented by a
  // 32-bit size_type and must not silently truncate.
  CHECK(*cbor::detail::length_to_size<std::uint32_t>(5) == 5u);
  CHECK(*cbor::detail::length_to_size<std::uint32_t>(0xFFFFFFFFull) == 0xFFFFFFFFu);
  CHECK(
    cbor::detail::length_to_size<std::uint32_t>(0x100000005ull).error() == error::string_too_long
  );  // 4 GiB + 5 truncates to 5 on a 32-bit target without the guard
  CHECK(*cbor::detail::length_to_size<std::uint64_t>(0x100000005ull) == 0x100000005ull);

  // End-to-end: a byte string declaring an enormous 8-byte length over a short
  // buffer is a clean error, never a wrong-length span.
  auto const buf{
    bytes_of(0x5B, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x05, 'h', 'e', 'l', 'l', 'o')
  };
  auto r{cbor::reader{buf}};
  CHECK(!r.read_bytes().has_value());
}

}  // namespace
