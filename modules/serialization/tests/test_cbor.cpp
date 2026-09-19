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

/**
 * @brief Builds a byte vector from integer literals.
 *
 * @tparam Bs Integer types of the literals.
 * @param bs Byte values, each narrowed to \c std::uint8_t.
 *
 * @return The bytes, in order.
 *
 * @pre Every value fits in a byte.
 * @post None.
 */
template <typename... Bs>
[[nodiscard]] auto bytes_of(Bs... bs) -> std::vector<std::byte> {
  return {static_cast<std::byte>(static_cast<std::uint8_t>(bs))...};
}

/**
 * @brief Whether a writer's emitted bytes equal \p expected.
 *
 * @param w Writer whose emitted prefix is compared.
 * @param expected Expected byte sequence.
 *
 * @return \c true when the sizes and every byte match.
 *
 * @pre None.
 * @post None.
 */
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
  CHECK(*r.peek_type() == cbor::type::undefined);
  REQUIRE(r.read_undefined().has_value());
  CHECK(r.at_end());
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
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_bytes(std::span<std::byte const>{}).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x40}));
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto const body{
      std::array<std::byte, 4>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}
    };
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_bytes(std::span<std::byte const>{body}).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x44, 0x01, 0x02, 0x03, 0x04}));
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_string("").has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x60}));
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_string("a").has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x61, 0x61}));
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_string("IETF").has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x64, 0x49, 0x45, 0x54, 0x46}));
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_string("\"\\").has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x62, 0x22, 0x5c}));
  }
}

TEST_CASE("nexenne::serialization::cbor matches RFC 8949 Appendix A container vectors") {
  {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_array_header(0).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x80}));
  }
  {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_array_header(3).has_value());
    REQUIRE(w.write_uint(1).has_value());
    REQUIRE(w.write_uint(2).has_value());
    REQUIRE(w.write_uint(3).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0x83, 0x01, 0x02, 0x03}));
  }
  {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_map_header(0).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0xa0}));
  }
  {
    auto buf{std::array<std::byte, 16>{}};
    auto w{cbor::writer{buf}};
    REQUIRE(w.write_map_header(2).has_value());
    REQUIRE(w.write_uint(1).has_value());
    REQUIRE(w.write_uint(2).has_value());
    REQUIRE(w.write_uint(3).has_value());
    REQUIRE(w.write_uint(4).has_value());
    CHECK(written_equals(w, std::vector<std::uint8_t>{0xa2, 0x01, 0x02, 0x03, 0x04}));
  }
  {
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
    {{0xf9, 0x00, 0x00}, 0.0},
    {{0xf9, 0x3c, 0x00}, 1.0},
    {{0xf9, 0x3e, 0x00}, 1.5},
    {{0xf9, 0x7b, 0xff}, 65504.0},
    {{0xfa, 0x47, 0xc3, 0x50, 0x00}, 100000.0},
    {{0xfa, 0x7f, 0x7f, 0xff, 0xff}, 3.4028234663852886e+38},
    {{0xfb, 0x3f, 0xf1, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a}, 1.1},
    {{0xfb, 0x40, 0x09, 0x21, 0xfb, 0x54, 0x44, 0x2d, 0x18}, 3.141592653589793},
    {{0xf9, 0x00, 0x01}, 5.960464477539063e-08},
    {{0xf9, 0x04, 0x00}, 6.103515625e-05},
    {{0xf9, 0xc4, 0x00}, -4.0},
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
  {
    auto const raw{bytes_of(0xf9, 0x7c, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isinf(*d));
    CHECK(*d > 0.0);
  }
  {
    auto const raw{bytes_of(0xf9, 0xfc, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isinf(*d));
    CHECK(*d < 0.0);
  }
  {
    auto const raw{bytes_of(0xf9, 0x7e, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isnan(*d));
  }
  {
    auto const raw{bytes_of(0xfa, 0x7f, 0x80, 0x00, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const d{r.read_float()};
    REQUIRE(d.has_value());
    CHECK(std::isinf(*d));
  }
  {
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
    {23, 1, 0x17},
    {24, 2, 0x18},
    {255, 2, 0x18},
    {256, 3, 0x19},
    {65535, 3, 0x19},
    {65536, 5, 0x1a},
    {4294967295ULL, 5, 0x1a},
    {4294967296ULL, 9, 0x1b},
    {18446744073709551615ULL, 9, 0x1b},
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
    {-24, 1, 0x37},
    {-25, 2, 0x38},
    {-256, 2, 0x38},
    {-257, 3, 0x39},
    {-65536, 3, 0x39},
    {-65537, 5, 0x3a},
    {-4294967296LL, 5, 0x3a},
    {-4294967297LL, 9, 0x3b},
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
  {
    auto const raw{bytes_of(0x1b, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const v{r.read_int()};
    CHECK(!v.has_value());
    CHECK(v.error() == error::type_mismatch);
  }
  {
    auto const raw{bytes_of(0x3b, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const v{r.read_int()};
    CHECK(!v.has_value());
    CHECK(v.error() == error::type_mismatch);
  }
  {
    auto const raw{bytes_of(0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    CHECK(*r.read_uint() == 18446744073709551615ULL);
  }
}

TEST_CASE("nexenne::serialization::cbor decodes the smallest subnormal half exactly") {
  // f9 00 01 = 2^-24, the smallest positive subnormal half.
  auto const raw{bytes_of(0xf9, 0x00, 0x01)};
  auto r{cbor::reader{std::span<std::byte const>{raw}}};
  auto const d{r.read_float()};
  REQUIRE(d.has_value());
  CHECK(*d == 5.9604644775390625e-08);
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
  {
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
    CHECK(failed);
  }
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
  auto const cases{std::vector<std::vector<std::byte>>{
    bytes_of(0x18),
    bytes_of(0x19, 0x01),
    bytes_of(0x1a, 0x00, 0x0f, 0x42),
    bytes_of(0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4),
  }};
  for (auto const& raw : cases) {
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const v{r.read_uint()};
    CHECK(!v.has_value());
    CHECK(v.error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::cbor rejects a length claiming more than remains") {
  {
    auto const raw{bytes_of(0x6a, 'a', 'b', 'c')};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const s{r.read_string()};
    CHECK(!s.has_value());
    CHECK(s.error() == error::buffer_underrun);
  }
  {
    auto const raw{bytes_of(0x48, 0x01, 0x02)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const b{r.read_bytes()};
    CHECK(!b.has_value());
    CHECK(b.error() == error::buffer_underrun);
  }
  {
    auto const raw{bytes_of(0x7b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const s{r.read_string()};
    CHECK(!s.has_value());
    CHECK(s.error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::cbor rejects reserved additional-info values") {
  for (auto const ai : {std::uint8_t{0x1c}, std::uint8_t{0x1d}, std::uint8_t{0x1e}}) {
    auto const raw{bytes_of(ai)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const v{r.read_uint()};
    CHECK(!v.has_value());
    CHECK(v.error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::cbor rejects the indefinite-length marker") {
  {
    auto const raw{bytes_of(0x9f)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const a{r.read_array_header()};
    CHECK(!a.has_value());
    CHECK(a.error() == error::invalid_input);
  }
  {
    auto const raw{bytes_of(0x7f)};
    auto r{cbor::reader{std::span<std::byte const>{raw}}};
    auto const s{r.read_string()};
    CHECK(!s.has_value());
    CHECK(s.error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::cbor rejects unsupported major type 6 (tags)") {
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
    bytes_of(0xf9, 0x3c),
    bytes_of(0xfa, 0x47, 0xc3),
    bytes_of(0xfb, 0x3f, 0xf1, 0x99, 0x99),
    bytes_of(0xf9),
    bytes_of(0xfa),
    bytes_of(0xfb),
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
  CHECK(r.read_null().error() == error::type_mismatch);
}

TEST_CASE("nexenne::serialization::cbor typed reads reject mismatched major types") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_uint(5).has_value());

  {
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_string().error() == error::type_mismatch);
  }
  {
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_bytes().error() == error::type_mismatch);
  }
  {
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_array_header().error() == error::type_mismatch);
  }
  {
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_map_header().error() == error::type_mismatch);
  }
  {
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_bool().error() == error::type_mismatch);
  }
  {
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_float().error() == error::type_mismatch);
  }
  {
    auto r{cbor::reader{w.written()}};
    CHECK(r.read_null().error() == error::type_mismatch);
  }
}

TEST_CASE("nexenne::serialization::cbor writer reports buffer_full at each boundary") {
  {
    auto buf{std::array<std::byte, 0>{}};
    auto w{cbor::writer{buf}};
    auto const r{w.write_uint(0)};
    CHECK(!r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  {
    auto buf{std::array<std::byte, 2>{}};
    auto w{cbor::writer{buf}};
    auto const r{w.write_string("abcd")};
    CHECK(!r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  {
    auto buf{std::array<std::byte, 8>{}};
    auto w{cbor::writer{buf}};
    auto const r{w.write_uint(18446744073709551615ULL)};
    CHECK(!r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  {
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
  auto const big{std::string(65540, 'q')};
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
  CHECK(*cbor::detail::length_to_size<std::uint32_t>(5) == 5u);
  CHECK(*cbor::detail::length_to_size<std::uint32_t>(0xFFFFFFFFull) == 0xFFFFFFFFu);
  CHECK(
    cbor::detail::length_to_size<std::uint32_t>(0x100000005ull).error() == error::string_too_long
  );
  CHECK(*cbor::detail::length_to_size<std::uint64_t>(0x100000005ull) == 0x100000005ull);

  auto const buf{
    bytes_of(0x5B, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x05, 'h', 'e', 'l', 'l', 'o')
  };
  auto r{cbor::reader{buf}};
  CHECK(!r.read_bytes().has_value());
}

TEST_CASE("nexenne::serialization::cbor read_undefined round-trips write_undefined") {
  auto buf{std::array<std::byte, 4>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_undefined().has_value());
  CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xF7);

  auto r{cbor::reader{w.written()}};
  CHECK(*r.peek_type() == cbor::type::undefined);
  REQUIRE(r.read_undefined().has_value());
  CHECK(r.at_end());

  auto rn{cbor::reader{w.written()}};
  CHECK(rn.read_null().error() == error::type_mismatch);
  auto const nul{bytes_of(0xF6)};
  auto ru{cbor::reader{nul}};
  CHECK(ru.read_undefined().error() == error::type_mismatch);
}

TEST_CASE("nexenne::serialization::cbor skip_value advances past exactly one item") {
  SUBCASE("over an undefined") {
    auto const buf{bytes_of(0xF7, 0x09)};
    auto r{cbor::reader{buf}};
    REQUIRE(r.skip_value().has_value());
    CHECK(*r.read_uint() == 9u);
    CHECK(r.at_end());
  }
  SUBCASE("over an int") {
    auto const buf{bytes_of(0x18, 0x2A, 0x05)};
    auto r{cbor::reader{buf}};
    REQUIRE(r.skip_value().has_value());
    CHECK(*r.read_uint() == 5u);
    CHECK(r.at_end());
  }
  SUBCASE("over a text string") {
    auto const buf{bytes_of(0x63, 'f', 'o', 'o', 0x05)};
    auto r{cbor::reader{buf}};
    REQUIRE(r.skip_value().has_value());
    CHECK(*r.read_uint() == 5u);
    CHECK(r.at_end());
  }
  SUBCASE("over a nested array") {
    auto const buf{bytes_of(0x82, 0x01, 0x82, 0x02, 0x03, 0x07)};
    auto r{cbor::reader{buf}};
    REQUIRE(r.skip_value().has_value());
    CHECK(*r.read_uint() == 7u);
    CHECK(r.at_end());
  }
  SUBCASE("over a map") {
    auto const buf{bytes_of(0xA1, 0x01, 0x02, 0x07)};
    auto r{cbor::reader{buf}};
    REQUIRE(r.skip_value().has_value());
    CHECK(*r.read_uint() == 7u);
    CHECK(r.at_end());
  }
  SUBCASE("a truncated item reports underrun") {
    auto const buf{bytes_of(0x63, 'f')};
    auto r{cbor::reader{buf}};
    CHECK(r.skip_value().error() == error::buffer_underrun);
  }
  SUBCASE("a tag is rejected") {
    auto const buf{bytes_of(0xC0, 0x00)};
    auto r{cbor::reader{buf}};
    CHECK(r.skip_value().error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::cbor write_string leaves bytes_written unchanged on overflow") {
  auto buf{std::array<std::byte, 2>{}};
  auto w{cbor::writer{buf}};
  auto const r{w.write_string("abcd")};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == error::buffer_full);
  CHECK(w.bytes_written() == 0);
}

TEST_CASE("nexenne::serialization::cbor write_bytes leaves bytes_written unchanged on overflow") {
  auto buf{std::array<std::byte, 2>{}};
  auto w{cbor::writer{buf}};
  auto const payload{std::array<std::byte, 4>{}};
  auto const r{w.write_bytes(std::span<std::byte const>{payload})};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == error::buffer_full);
  CHECK(w.bytes_written() == 0);
}

}  // namespace
