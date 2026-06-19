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

// Build a byte vector from a brace-init list of integer literals so spec
// vectors read like the wire bytes they assert.
[[nodiscard]] auto bytes(std::initializer_list<int> const xs) -> std::vector<std::byte> {
  auto out{std::vector<std::byte>{}};
  out.reserve(xs.size());
  for (auto const x : xs)
    out.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(x)));
  return out;
}

// Span view over the bytes a writer emitted.
[[nodiscard]] auto
written_equals(msgpack::writer const& w, std::initializer_list<int> const expected) -> bool {
  auto const got{w.written()};
  auto const exp{bytes(expected)};
  if (got.size() != exp.size())
    return false;
  for (std::size_t i{0}; i < exp.size(); ++i) {
    if (got[i] != exp[i])
      return false;
  }
  return true;
}

[[nodiscard]] auto as_span(std::vector<std::byte> const& v) -> std::span<std::byte const> {
  return std::span<std::byte const>{v.data(), v.size()};
}

TEST_CASE("nexenne::serialization::msgpack canonical encodings - exact bytes") {
  auto buf{std::array<std::byte, 64>{}};

  SUBCASE("integer 0 encodes to single byte 0x00") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(0).has_value());
    CHECK(written_equals(w, {0x00}));
  }
  SUBCASE("integer 127 encodes to single byte 0x7f") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(127).has_value());
    CHECK(written_equals(w, {0x7f}));
  }
  SUBCASE("integer -1 encodes to single byte 0xff") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(-1).has_value());
    CHECK(written_equals(w, {0xff}));
  }
  SUBCASE("integer -32 encodes to single byte 0xe0") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(-32).has_value());
    CHECK(written_equals(w, {0xe0}));
  }
  SUBCASE("integer 128 encodes to uint8 0xcc 0x80") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(128).has_value());
    CHECK(written_equals(w, {0xcc, 0x80}));
  }
  SUBCASE("integer 255 encodes to uint8 0xcc 0xff") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(255).has_value());
    CHECK(written_equals(w, {0xcc, 0xff}));
  }
  SUBCASE("integer 256 encodes to uint16 0xcd 0x01 0x00") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(256).has_value());
    CHECK(written_equals(w, {0xcd, 0x01, 0x00}));
  }
  SUBCASE("nil encodes to 0xc0") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_nil().has_value());
    CHECK(written_equals(w, {0xc0}));
  }
  SUBCASE("false encodes to 0xc2") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_bool(false).has_value());
    CHECK(written_equals(w, {0xc2}));
  }
  SUBCASE("true encodes to 0xc3") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_bool(true).has_value());
    CHECK(written_equals(w, {0xc3}));
  }
  SUBCASE("empty string encodes to fixstr 0xa0") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_string("").has_value());
    CHECK(written_equals(w, {0xa0}));
  }
  SUBCASE("single-char string encodes to 0xa1 0x61") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_string("a").has_value());
    CHECK(written_equals(w, {0xa1, 0x61}));
  }
  SUBCASE("empty array encodes to fixarray 0x90") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_array_header(0).has_value());
    CHECK(written_equals(w, {0x90}));
  }
  SUBCASE("empty map encodes to fixmap 0x80") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_map_header(0).has_value());
    CHECK(written_equals(w, {0x80}));
  }
}

TEST_CASE("nexenne::serialization::msgpack canonical decodings - raw bytes back to values") {
  SUBCASE("0x00 decodes to integer 0") {
    auto const v{bytes({0x00})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.peek_type() == msgpack::type::integer);
    CHECK(*r.read_int() == 0);
    CHECK(r.at_end());
  }
  SUBCASE("0x7f decodes to integer 127") {
    auto const v{bytes({0x7f})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.read_int() == 127);
  }
  SUBCASE("0xff decodes to integer -1") {
    auto const v{bytes({0xff})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.read_int() == -1);
  }
  SUBCASE("0xe0 decodes to integer -32") {
    auto const v{bytes({0xe0})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.read_int() == -32);
  }
  SUBCASE("0xcc 0x80 decodes to integer 128") {
    auto const v{bytes({0xcc, 0x80})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.read_int() == 128);
    CHECK(r.at_end());
  }
  SUBCASE("0xcc 0xff decodes to integer 255") {
    auto const v{bytes({0xcc, 0xff})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.read_int() == 255);
  }
  SUBCASE("0xcd 0x01 0x00 decodes to integer 256") {
    auto const v{bytes({0xcd, 0x01, 0x00})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.read_int() == 256);
  }
  SUBCASE("0xc0 decodes to nil") {
    auto const v{bytes({0xc0})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.peek_type() == msgpack::type::nil);
    CHECK(r.read_nil().has_value());
    CHECK(r.at_end());
  }
  SUBCASE("0xc2 decodes to false") {
    auto const v{bytes({0xc2})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.peek_type() == msgpack::type::boolean);
    CHECK(*r.read_bool() == false);
  }
  SUBCASE("0xc3 decodes to true") {
    auto const v{bytes({0xc3})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.read_bool() == true);
  }
  SUBCASE("0xa0 decodes to empty string") {
    auto const v{bytes({0xa0})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.peek_type() == msgpack::type::string);
    CHECK(*r.read_string() == "");
    CHECK(r.at_end());
  }
  SUBCASE("0xa1 0x61 decodes to string a") {
    auto const v{bytes({0xa1, 0x61})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.read_string() == "a");
  }
  SUBCASE("0x90 decodes to empty array header") {
    auto const v{bytes({0x90})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.peek_type() == msgpack::type::array_header);
    CHECK(*r.read_array_header() == 0u);
  }
  SUBCASE("0x80 decodes to empty map header") {
    auto const v{bytes({0x80})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(*r.peek_type() == msgpack::type::map_header);
    CHECK(*r.read_map_header() == 0u);
  }
}

TEST_CASE("nexenne::serialization::msgpack positive fixint covers 0..127 single byte") {
  for (int i{0}; i <= 127; ++i) {
    auto buf{std::array<std::byte, 8>{}};
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(i).has_value());
    CHECK(w.bytes_written() == 1);
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == static_cast<std::uint8_t>(i));

    auto r{msgpack::reader{w.written()}};
    CHECK(*r.read_int() == i);
  }
}

TEST_CASE("nexenne::serialization::msgpack negative fixint covers -1..-32 single byte") {
  for (int i{-1}; i >= -32; --i) {
    auto buf{std::array<std::byte, 8>{}};
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(i).has_value());
    CHECK(w.bytes_written() == 1);
    // negative fixint occupies 0xE0..0xFF
    auto const b{static_cast<std::uint8_t>(w.written()[0])};
    CHECK(b >= 0xE0);

    auto r{msgpack::reader{w.written()}};
    CHECK(*r.read_int() == i);
  }
}

TEST_CASE("nexenne::serialization::msgpack unsigned width boundaries pick smallest form") {
  auto buf{std::array<std::byte, 16>{}};

  struct case_t {
    std::uint64_t value;
    std::size_t size;
    int prefix;  // -1 means single-byte fixint (no separate prefix)
  };

  auto const cases{std::array<case_t, 9>{{
    {0x7F, 1, -1},                     // positive fixint max
    {0x80, 2, 0xCC},                   // first uint8
    {0xFF, 2, 0xCC},                   // uint8 max
    {0x100, 3, 0xCD},                  // first uint16
    {0xFFFF, 3, 0xCD},                 // uint16 max
    {0x10000, 5, 0xCE},                // first uint32
    {0xFFFFFFFFu, 5, 0xCE},            // uint32 max
    {0x100000000ULL, 9, 0xCF},         // first uint64
    {0xFFFFFFFFFFFFFFFFULL, 9, 0xCF},  // uint64 max
  }}};

  for (auto const& c : cases) {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_uint(c.value).has_value());
    CHECK(w.bytes_written() == c.size);
    if (c.prefix >= 0)
      CHECK(static_cast<std::uint8_t>(w.written()[0]) == static_cast<std::uint8_t>(c.prefix));

    auto r{msgpack::reader{w.written()}};
    auto const got{r.read_int()};
    REQUIRE(got.has_value());
    // read_int widens to int64; uint64 above INT64_MAX wraps (documented).
    CHECK(static_cast<std::uint64_t>(*got) == c.value);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack signed negative width boundaries pick smallest form") {
  auto buf{std::array<std::byte, 16>{}};

  struct case_t {
    std::int64_t value;
    std::size_t size;
    int prefix;  // -1 = negative fixint
  };

  auto const cases{std::array<case_t, 9>{{
    {-1, 1, -1},               // negative fixint
    {-32, 1, -1},              // negative fixint min
    {-33, 2, 0xD0},            // first int8
    {-128, 2, 0xD0},           // int8 min
    {-129, 3, 0xD1},           // first int16
    {-32768, 3, 0xD1},         // int16 min
    {-32769, 5, 0xD2},         // first int32
    {-2147483648LL, 5, 0xD2},  // int32 min
    {-2147483649LL, 9, 0xD3},  // first int64
  }}};

  for (auto const& c : cases) {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(c.value).has_value());
    CHECK(w.bytes_written() == c.size);
    if (c.prefix >= 0)
      CHECK(static_cast<std::uint8_t>(w.written()[0]) == static_cast<std::uint8_t>(c.prefix));

    auto r{msgpack::reader{w.written()}};
    CHECK(*r.read_int() == c.value);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack int64 extremes round trip") {
  auto buf{std::array<std::byte, 16>{}};
  auto const lo{std::numeric_limits<std::int64_t>::min()};
  auto const hi{std::numeric_limits<std::int64_t>::max()};

  SUBCASE("INT64_MIN as int64 prefix 0xd3") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(lo).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xD3);
    CHECK(w.bytes_written() == 9);
    auto r{msgpack::reader{w.written()}};
    CHECK(*r.read_int() == lo);
  }
  SUBCASE("INT64_MAX routes through write_uint as uint64") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_int(hi).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xCF);
    auto r{msgpack::reader{w.written()}};
    CHECK(*r.read_int() == hi);
  }
}

TEST_CASE("nexenne::serialization::msgpack string forms round trip across length boundaries") {
  struct case_t {
    std::size_t len;
    int prefix;  // first byte; for fixstr it carries length in low 5 bits
    std::size_t header;
  };

  // 31 = fixstr max, 32 = first str8, 255 = str8 max, 256 = first str16,
  // 65535 = str16 max, 65536 = first str32.
  auto const cases{std::array<case_t, 7>{{
    {0, 0xA0, 1},
    {31, 0xBF, 1},  // 0xA0 | 31
    {32, 0xD9, 2},
    {255, 0xD9, 2},
    {256, 0xDA, 3},
    {65535, 0xDA, 3},
    {65536, 0xDB, 5},
  }}};

  for (auto const& c : cases) {
    auto const s{std::string(c.len, 'z')};
    auto buf{std::vector<std::byte>(c.len + 8)};
    auto w{msgpack::writer{std::span<std::byte>{buf}}};
    REQUIRE(w.write_string(s).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == static_cast<std::uint8_t>(c.prefix));
    CHECK(w.bytes_written() == c.header + c.len);

    auto r{msgpack::reader{w.written()}};
    CHECK(*r.peek_type() == msgpack::type::string);
    auto const out{r.read_string()};
    REQUIRE(out.has_value());
    CHECK(out->size() == c.len);
    CHECK(*out == s);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack string with embedded NUL and high bytes round trips") {
  auto const s{std::string{"a\0b\xff\x01", 5}};
  auto buf{std::array<std::byte, 32>{}};
  auto w{msgpack::writer{buf}};
  REQUIRE(w.write_string(s).has_value());

  auto r{msgpack::reader{w.written()}};
  auto const out{r.read_string()};
  REQUIRE(out.has_value());
  CHECK(out->size() == 5);
  CHECK(*out == s);
}

TEST_CASE("nexenne::serialization::msgpack binary forms round trip across length boundaries") {
  struct case_t {
    std::size_t len;
    int prefix;
    std::size_t header;
  };

  // bin has no fixbin; smallest is bin8.
  auto const cases{std::array<case_t, 6>{{
    {0, 0xC4, 2},
    {255, 0xC4, 2},
    {256, 0xC5, 3},
    {65535, 0xC5, 3},
    {65536, 0xC6, 5},
    {70000, 0xC6, 5},
  }}};

  for (auto const& c : cases) {
    auto blob{std::vector<std::byte>(c.len)};
    for (std::size_t i{0}; i < c.len; ++i)
      blob[i] = static_cast<std::byte>(static_cast<std::uint8_t>(i & 0xFF));

    auto buf{std::vector<std::byte>(c.len + 8)};
    auto w{msgpack::writer{std::span<std::byte>{buf}}};
    REQUIRE(w.write_binary(std::span<std::byte const>{blob}).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == static_cast<std::uint8_t>(c.prefix));
    CHECK(w.bytes_written() == c.header + c.len);

    auto r{msgpack::reader{w.written()}};
    CHECK(*r.peek_type() == msgpack::type::binary);
    auto const out{r.read_binary()};
    REQUIRE(out.has_value());
    REQUIRE(out->size() == c.len);
    if (c.len > 0) {
      CHECK((*out)[0] == blob[0]);
      CHECK((*out)[c.len - 1] == blob[c.len - 1]);
    }
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack array header forms round trip across boundaries") {
  struct case_t {
    std::uint32_t n;
    int prefix;
    std::size_t size;
  };

  auto const cases{std::array<case_t, 6>{{
    {0, 0x90, 1},
    {15, 0x9F, 1},     // fixarray max
    {16, 0xDC, 3},     // first array16
    {65535, 0xDC, 3},  // array16 max
    {65536, 0xDD, 5},  // first array32
    {100000, 0xDD, 5},
  }}};

  for (auto const& c : cases) {
    auto buf{std::array<std::byte, 8>{}};
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_array_header(c.n).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == static_cast<std::uint8_t>(c.prefix));
    CHECK(w.bytes_written() == c.size);

    auto r{msgpack::reader{w.written()}};
    CHECK(*r.peek_type() == msgpack::type::array_header);
    CHECK(*r.read_array_header() == c.n);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack map header forms round trip across boundaries") {
  struct case_t {
    std::uint32_t n;
    int prefix;
    std::size_t size;
  };

  auto const cases{std::array<case_t, 6>{{
    {0, 0x80, 1},
    {15, 0x8F, 1},     // fixmap max
    {16, 0xDE, 3},     // first map16
    {65535, 0xDE, 3},  // map16 max
    {65536, 0xDF, 5},  // first map32
    {100000, 0xDF, 5},
  }}};

  for (auto const& c : cases) {
    auto buf{std::array<std::byte, 8>{}};
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_map_header(c.n).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == static_cast<std::uint8_t>(c.prefix));
    CHECK(w.bytes_written() == c.size);

    auto r{msgpack::reader{w.written()}};
    CHECK(*r.peek_type() == msgpack::type::map_header);
    CHECK(*r.read_map_header() == c.n);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack float32 and float64 prefixes and round trip") {
  auto buf{std::array<std::byte, 16>{}};

  SUBCASE("float32 uses 0xca prefix, 5 bytes") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_float32(1.5F).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xCA);
    CHECK(w.bytes_written() == 5);
    auto r{msgpack::reader{w.written()}};
    CHECK(*r.peek_type() == msgpack::type::floating);
    CHECK(*r.read_float() == doctest::Approx(1.5));
  }
  SUBCASE("float64 uses 0xcb prefix, 9 bytes") {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_float64(3.141592653589793).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xCB);
    CHECK(w.bytes_written() == 9);
    auto r{msgpack::reader{w.written()}};
    CHECK(*r.read_float() == doctest::Approx(3.141592653589793));
  }
}

TEST_CASE("nexenne::serialization::msgpack float64 specials round trip bit-exact") {
  auto buf{std::array<std::byte, 16>{}};

  auto roundtrip64{[&](double const v) -> double {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_float64(v).has_value());
    auto r{msgpack::reader{w.written()}};
    auto const got{r.read_float()};
    REQUIRE(got.has_value());
    return *got;
  }};

  CHECK(std::isnan(roundtrip64(std::numeric_limits<double>::quiet_NaN())));
  CHECK(
    roundtrip64(std::numeric_limits<double>::infinity()) == std::numeric_limits<double>::infinity()
  );
  CHECK(
    roundtrip64(-std::numeric_limits<double>::infinity())
    == -std::numeric_limits<double>::infinity()
  );

  // -0.0 must survive bit-exact: equal to 0.0 by ==, but 1/-0.0 is -inf.
  auto const neg_zero{roundtrip64(-0.0)};
  CHECK(neg_zero == 0.0);
  CHECK(std::signbit(neg_zero));
  CHECK(std::isinf(1.0 / neg_zero));
  CHECK((1.0 / neg_zero) < 0.0);
}

TEST_CASE("nexenne::serialization::msgpack float32 specials round trip bit-exact") {
  auto buf{std::array<std::byte, 16>{}};

  auto roundtrip32{[&](float const v) -> double {
    auto w{msgpack::writer{buf}};
    REQUIRE(w.write_float32(v).has_value());
    auto r{msgpack::reader{w.written()}};
    auto const got{r.read_float()};  // widened to double
    REQUIRE(got.has_value());
    return *got;
  }};

  CHECK(std::isnan(roundtrip32(std::numeric_limits<float>::quiet_NaN())));
  CHECK(
    roundtrip32(std::numeric_limits<float>::infinity()) == std::numeric_limits<double>::infinity()
  );
  CHECK(
    roundtrip32(-std::numeric_limits<float>::infinity()) == -std::numeric_limits<double>::infinity()
  );

  auto const neg_zero{roundtrip32(-0.0F)};
  CHECK(neg_zero == 0.0);
  CHECK(std::signbit(neg_zero));
}

TEST_CASE("nexenne::serialization::msgpack nested document round trip") {
  auto buf{std::array<std::byte, 256>{}};
  auto w{msgpack::writer{buf}};
  // {"id": 42, "tags": ["a", "b"], "meta": {"ok": true, "v": nil}}
  REQUIRE(w.write_map_header(3).has_value());
  REQUIRE(w.write_string("id").has_value());
  REQUIRE(w.write_int(42).has_value());
  REQUIRE(w.write_string("tags").has_value());
  REQUIRE(w.write_array_header(2).has_value());
  REQUIRE(w.write_string("a").has_value());
  REQUIRE(w.write_string("b").has_value());
  REQUIRE(w.write_string("meta").has_value());
  REQUIRE(w.write_map_header(2).has_value());
  REQUIRE(w.write_string("ok").has_value());
  REQUIRE(w.write_bool(true).has_value());
  REQUIRE(w.write_string("v").has_value());
  REQUIRE(w.write_nil().has_value());

  auto r{msgpack::reader{w.written()}};
  CHECK(*r.read_map_header() == 3u);
  CHECK(*r.read_string() == "id");
  CHECK(*r.read_int() == 42);
  CHECK(*r.read_string() == "tags");
  CHECK(*r.read_array_header() == 2u);
  CHECK(*r.read_string() == "a");
  CHECK(*r.read_string() == "b");
  CHECK(*r.read_string() == "meta");
  CHECK(*r.read_map_header() == 2u);
  CHECK(*r.read_string() == "ok");
  CHECK(*r.read_bool() == true);
  CHECK(*r.read_string() == "v");
  REQUIRE(r.read_nil().has_value());
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::msgpack peek_type classifies every prefix") {
  struct case_t {
    int byte;
    msgpack::type kind;
  };

  auto const cases{std::array<case_t, 22>{{
    {0x00, msgpack::type::integer},                                       // positive fixint
    {0x7F, msgpack::type::integer},      {0xE0, msgpack::type::integer},  // negative fixint
    {0xFF, msgpack::type::integer},      {0xC0, msgpack::type::nil},
    {0xC2, msgpack::type::boolean},      {0xC3, msgpack::type::boolean},
    {0xCA, msgpack::type::floating},     {0xCB, msgpack::type::floating},
    {0xCC, msgpack::type::integer},      {0xCD, msgpack::type::integer},
    {0xCE, msgpack::type::integer},      {0xCF, msgpack::type::integer},
    {0xD0, msgpack::type::integer},      {0xD1, msgpack::type::integer},
    {0xD2, msgpack::type::integer},      {0xD3, msgpack::type::integer},
    {0xA0, msgpack::type::string},  // fixstr
    {0xD9, msgpack::type::string},       {0xC4, msgpack::type::binary},
    {0x90, msgpack::type::array_header},  // fixarray
    {0x80, msgpack::type::map_header},    // fixmap
  }}};

  for (auto const& c : cases) {
    auto const v{bytes({c.byte})};
    auto r{msgpack::reader{as_span(v)}};
    auto const t{r.peek_type()};
    REQUIRE(t.has_value());
    CHECK(*t == c.kind);
    CHECK(r.bytes_read() == 0);  // peek does not consume
  }
}

TEST_CASE("nexenne::serialization::msgpack peek_type rejects reserved 0xc1 with invalid_input") {
  auto const v{bytes({0xC1})};
  auto r{msgpack::reader{as_span(v)}};
  auto const t{r.peek_type()};
  CHECK(!t.has_value());
  CHECK(t.error() == error::invalid_input);
}

TEST_CASE("nexenne::serialization::msgpack peek_type rejects unsupported ext prefixes") {
  // ext family 0xC7..0xC9 and fixext 0xD4..0xD8 are not supported.
  for (int b : {0xC7, 0xC8, 0xC9, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8}) {
    auto const v{bytes({b})};
    auto r{msgpack::reader{as_span(v)}};
    auto const t{r.peek_type()};
    CHECK(!t.has_value());
    CHECK(t.error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::msgpack reads reject mismatched types cleanly") {
  SUBCASE("read_nil on non-nil yields type_mismatch and does not advance") {
    auto const v{bytes({0xC3})};  // true
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_nil()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::type_mismatch);
    CHECK(r.bytes_read() == 0);
  }
  SUBCASE("read_bool on non-bool yields type_mismatch") {
    auto const v{bytes({0xC0})};  // nil
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_bool()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::type_mismatch);
  }
  SUBCASE("read_int on a string yields type_mismatch and rewinds") {
    auto const v{bytes({0xA1, 0x78})};  // "x"
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_int()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::type_mismatch);
    CHECK(r.bytes_read() == 0);  // rewound to before prefix
  }
  SUBCASE("read_float on an integer yields type_mismatch") {
    auto const v{bytes({0x05})};
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_float()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::type_mismatch);
  }
  SUBCASE("read_string on an array header yields type_mismatch and rewinds") {
    auto const v{bytes({0x91})};  // fixarray of 1
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_string()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::type_mismatch);
    CHECK(r.bytes_read() == 0);
  }
  SUBCASE("read_binary on a string yields type_mismatch and rewinds") {
    auto const v{bytes({0xA0})};  // empty string
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_binary()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::type_mismatch);
    CHECK(r.bytes_read() == 0);
  }
  SUBCASE("read_array_header on a map header yields type_mismatch and rewinds") {
    auto const v{bytes({0x81})};  // fixmap of 1
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_array_header()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::type_mismatch);
    CHECK(r.bytes_read() == 0);
  }
  SUBCASE("read_map_header on an array header yields type_mismatch and rewinds") {
    auto const v{bytes({0x91})};  // fixarray of 1
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_map_header()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::type_mismatch);
    CHECK(r.bytes_read() == 0);
  }
}

TEST_CASE("nexenne::serialization::msgpack readers on empty input never read OOB") {
  auto const empty{std::span<std::byte const>{}};

  {
    auto r{msgpack::reader{empty}};
    CHECK(r.at_end());
    CHECK(r.peek_type().error() == error::buffer_underrun);
  }
  {
    auto r{msgpack::reader{empty}};
    CHECK(r.read_nil().error() == error::type_mismatch);  // !has(1) path
  }
  {
    auto r{msgpack::reader{empty}};
    CHECK(r.read_bool().error() == error::buffer_underrun);
  }
  {
    auto r{msgpack::reader{empty}};
    CHECK(r.read_int().error() == error::buffer_underrun);
  }
  {
    auto r{msgpack::reader{empty}};
    CHECK(r.read_float().error() == error::buffer_underrun);
  }
  {
    auto r{msgpack::reader{empty}};
    CHECK(r.read_string().error() == error::buffer_underrun);
  }
  {
    auto r{msgpack::reader{empty}};
    CHECK(r.read_binary().error() == error::buffer_underrun);
  }
  {
    auto r{msgpack::reader{empty}};
    CHECK(r.read_array_header().error() == error::buffer_underrun);
  }
  {
    auto r{msgpack::reader{empty}};
    CHECK(r.read_map_header().error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::msgpack every prefix of a full document errors cleanly") {
  // Build one document covering many wire forms, then feed every proper
  // prefix to a decoder that walks it. No prefix may read past its span.
  auto buf{std::array<std::byte, 256>{}};
  auto w{msgpack::writer{buf}};
  // 7 elements: uint, int, float64, float32, string, binary, map (the map's
  // key+value are the map's content, not extra array elements).
  REQUIRE(w.write_array_header(7).has_value());
  REQUIRE(w.write_uint(0x123456789ABCDEFULL).has_value());  // uint64
  REQUIRE(w.write_int(-30000).has_value());                 // int16
  REQUIRE(w.write_float64(2.5).has_value());                // float64
  REQUIRE(w.write_float32(1.25F).has_value());              // float32
  REQUIRE(w.write_string("hello world").has_value());       // fixstr
  auto const blob{std::array<std::byte, 3>{std::byte{1}, std::byte{2}, std::byte{3}}};
  REQUIRE(w.write_binary(std::span<std::byte const>{blob}).has_value());  // bin8
  REQUIRE(w.write_map_header(1).has_value());
  REQUIRE(w.write_string("k").has_value());
  REQUIRE(w.write_bool(true).has_value());

  auto const full{std::vector<std::byte>(w.written().begin(), w.written().end())};

  // A walker that drains the document structurally. It must either succeed
  // (only on the full buffer) or fail with a bounds error - never crash.
  auto walk{[](std::span<std::byte const> const data) -> bool {
    auto r{msgpack::reader{data}};
    auto const n{r.read_array_header()};
    if (!n)
      return false;
    for (std::uint32_t i{0}; i < *n; ++i) {
      auto const t{r.peek_type()};
      if (!t)
        return false;
      switch (*t) {
        case msgpack::type::nil:
          if (!r.read_nil())
            return false;
          break;
        case msgpack::type::boolean:
          if (!r.read_bool())
            return false;
          break;
        case msgpack::type::integer:
          if (!r.read_int())
            return false;
          break;
        case msgpack::type::floating:
          if (!r.read_float())
            return false;
          break;
        case msgpack::type::string:
          if (!r.read_string())
            return false;
          break;
        case msgpack::type::binary:
          if (!r.read_binary())
            return false;
          break;
        case msgpack::type::map_header: {
          auto const m{r.read_map_header()};
          if (!m)
            return false;
          // each pair: key string + bool value (matches the doc above)
          for (std::uint32_t j{0}; j < *m; ++j) {
            if (!r.read_string())
              return false;
            if (!r.read_bool())
              return false;
          }
          break;
        }
        case msgpack::type::array_header: {
          auto const a{r.read_array_header()};
          if (!a)
            return false;
          break;
        }
      }
    }
    return r.at_end();
  }};

  // Every strict prefix must fail to fully walk; only the full doc succeeds.
  for (std::size_t len{0}; len < full.size(); ++len) {
    auto const prefix{std::span<std::byte const>{full.data(), len}};
    CHECK(walk(prefix) == false);
  }
  CHECK(walk(std::span<std::byte const>{full.data(), full.size()}) == true);
}

TEST_CASE("nexenne::serialization::msgpack truncated multi-byte headers report underrun") {
  SUBCASE("uint16 prefix with only one trailing byte") {
    auto const v{bytes({0xCD, 0x01})};  // claims 2 bytes, 1 present
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_int()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::buffer_underrun);
  }
  SUBCASE("uint64 prefix with no trailing bytes") {
    auto const v{bytes({0xCF})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_int().error() == error::buffer_underrun);
  }
  SUBCASE("int32 prefix truncated") {
    auto const v{bytes({0xD2, 0x00, 0x00})};  // claims 4, 2 present
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_int().error() == error::buffer_underrun);
  }
  SUBCASE("float32 prefix truncated") {
    auto const v{bytes({0xCA, 0x3F, 0x80})};  // claims 4, 2 present
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_float().error() == error::buffer_underrun);
  }
  SUBCASE("float64 prefix truncated") {
    auto const v{bytes({0xCB, 0x00})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_float().error() == error::buffer_underrun);
  }
  SUBCASE("str8 length byte missing") {
    auto const v{bytes({0xD9})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_string().error() == error::buffer_underrun);
  }
  SUBCASE("str16 length truncated") {
    auto const v{bytes({0xDA, 0x00})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_string().error() == error::buffer_underrun);
  }
  SUBCASE("bin8 length byte missing") {
    auto const v{bytes({0xC4})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_binary().error() == error::buffer_underrun);
  }
  SUBCASE("array16 count truncated") {
    auto const v{bytes({0xDC, 0x00})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_array_header().error() == error::buffer_underrun);
  }
  SUBCASE("map32 count truncated") {
    auto const v{bytes({0xDF, 0x00, 0x00})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_map_header().error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::msgpack oversized length claims are rejected, no OOB") {
  SUBCASE("fixstr claiming 5 bytes with body absent") {
    auto const v{bytes({0xA5})};  // fixstr length 5, zero body bytes
    auto r{msgpack::reader{as_span(v)}};
    auto const e{r.read_string()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::buffer_underrun);
  }
  SUBCASE("fixstr claiming more than present") {
    auto const v{bytes({0xA5, 0x61, 0x62})};  // wants 5, has 2
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_string().error() == error::buffer_underrun);
  }
  SUBCASE("str8 claiming 255 bytes with empty body") {
    auto const v{bytes({0xD9, 0xFF})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_string().error() == error::buffer_underrun);
  }
  SUBCASE("str16 claiming 0xFFFF with empty body") {
    auto const v{bytes({0xDA, 0xFF, 0xFF})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_string().error() == error::buffer_underrun);
  }
  SUBCASE("str32 claiming 0xFFFFFFFF with empty body") {
    auto const v{bytes({0xDB, 0xFF, 0xFF, 0xFF, 0xFF})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_string().error() == error::buffer_underrun);
  }
  SUBCASE("bin8 claiming 200 bytes with empty body") {
    auto const v{bytes({0xC4, 0xC8})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_binary().error() == error::buffer_underrun);
  }
  SUBCASE("bin32 claiming 0xFFFFFFFF with empty body") {
    auto const v{bytes({0xC6, 0xFF, 0xFF, 0xFF, 0xFF})};
    auto r{msgpack::reader{as_span(v)}};
    CHECK(r.read_binary().error() == error::buffer_underrun);
  }
}

TEST_CASE("nexenne::serialization::msgpack huge container counts do not over-read the header") {
  // A header may legitimately claim a huge element count; only the header
  // bytes are consumed. The danger is reading the count itself out of bounds,
  // which take() guards. The reader returns the count without touching bodies.
  SUBCASE("array32 with max count, header present") {
    auto const v{bytes({0xDD, 0xFF, 0xFF, 0xFF, 0xFF})};
    auto r{msgpack::reader{as_span(v)}};
    auto const n{r.read_array_header()};
    REQUIRE(n.has_value());
    CHECK(*n == 0xFFFFFFFFu);  // count returned; elements are the caller's job
    CHECK(r.at_end());
  }
  SUBCASE("map32 with max count, header present") {
    auto const v{bytes({0xDF, 0xFF, 0xFF, 0xFF, 0xFF})};
    auto r{msgpack::reader{as_span(v)}};
    auto const n{r.read_map_header()};
    REQUIRE(n.has_value());
    CHECK(*n == 0xFFFFFFFFu);
  }
}

TEST_CASE("nexenne::serialization::msgpack deeply nested arrays decode iteratively") {
  // 1000 nested single-element fixarrays followed by a final integer.
  constexpr int depth{1000};
  auto data{std::vector<std::byte>{}};
  data.reserve(depth + 1);
  for (int i{0}; i < depth; ++i)
    data.push_back(static_cast<std::byte>(0x91));  // fixarray of 1
  data.push_back(static_cast<std::byte>(0x2A));    // integer 42

  auto r{msgpack::reader{as_span(data)}};
  for (int i{0}; i < depth; ++i) {
    auto const n{r.read_array_header()};
    REQUIRE(n.has_value());
    CHECK(*n == 1u);
  }
  CHECK(*r.read_int() == 42);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::msgpack deeply nested fixarray headers truncated mid-way") {
  // All array headers, no leaf value: walking consumes every header then
  // underruns cleanly - no crash, no infinite loop.
  constexpr int depth{500};
  auto data{std::vector<std::byte>(depth, static_cast<std::byte>(0x91))};

  auto r{msgpack::reader{as_span(data)}};
  for (int i{0}; i < depth; ++i)
    REQUIRE(r.read_array_header().has_value());
  CHECK(r.at_end());
  // The next element does not exist.
  CHECK(r.peek_type().error() == error::buffer_underrun);
}

TEST_CASE("nexenne::serialization::msgpack writer reports buffer_full without moving cursor") {
  SUBCASE("nil into a zero-byte buffer") {
    auto buf{std::array<std::byte, 0>{}};
    auto w{msgpack::writer{buf}};
    auto const e{w.write_nil()};
    CHECK(!e.has_value());
    CHECK(e.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("uint8 into a one-byte buffer (needs 2)") {
    auto buf{std::array<std::byte, 1>{}};
    auto w{msgpack::writer{buf}};
    auto const e{w.write_uint(200)};
    CHECK(e.error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("float64 into an eight-byte buffer (needs 9)") {
    auto buf{std::array<std::byte, 8>{}};
    auto w{msgpack::writer{buf}};
    CHECK(w.write_float64(1.0).error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("string header fits but body does not") {
    auto buf{std::array<std::byte, 3>{}};  // header 1 + want 5
    auto w{msgpack::writer{buf}};
    CHECK(w.write_string("hello").error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("binary header fits but body does not") {
    auto buf{std::array<std::byte, 3>{}};
    auto const blob{std::array<std::byte, 8>{}};
    auto w{msgpack::writer{buf}};
    CHECK(w.write_binary(std::span<std::byte const>{blob}).error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("array16 header does not fit") {
    auto buf{std::array<std::byte, 2>{}};  // needs 3
    auto w{msgpack::writer{buf}};
    CHECK(w.write_array_header(100).error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
  SUBCASE("map32 header does not fit") {
    auto buf{std::array<std::byte, 4>{}};  // needs 5
    auto w{msgpack::writer{buf}};
    CHECK(w.write_map_header(100000).error() == error::buffer_full);
    CHECK(w.bytes_written() == 0);
  }
}

TEST_CASE("nexenne::serialization::msgpack writer bookkeeping: remaining, written view, reset") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{msgpack::writer{buf}};
  CHECK(w.bytes_remaining() == 16);
  REQUIRE(w.write_uint(0x100).has_value());  // 3 bytes
  CHECK(w.bytes_written() == 3);
  CHECK(w.bytes_remaining() == 13);
  CHECK(w.written().size() == 3);

  w.reset();
  CHECK(w.bytes_written() == 0);
  CHECK(w.bytes_remaining() == 16);
  REQUIRE(w.write_bool(true).has_value());
  CHECK(w.bytes_written() == 1);
}

TEST_CASE("nexenne::serialization::msgpack reader bookkeeping tracks the cursor") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{msgpack::writer{buf}};
  REQUIRE(w.write_int(1).has_value());        // 1 byte
  REQUIRE(w.write_uint(0x100).has_value());   // 3 bytes
  REQUIRE(w.write_string("hi").has_value());  // 1 + 2 = 3 bytes

  auto r{msgpack::reader{w.written()}};
  auto const total{w.bytes_written()};
  CHECK(r.bytes_read() == 0);
  CHECK(r.bytes_remaining() == total);
  CHECK_FALSE(r.at_end());

  REQUIRE(r.read_int().has_value());
  CHECK(r.bytes_read() == 1);
  REQUIRE(r.read_int().has_value());  // reads the uint16
  CHECK(r.bytes_read() == 4);
  CHECK(*r.read_string() == "hi");
  CHECK(r.bytes_read() == total);
  CHECK(r.at_end());
}

TEST_CASE("nexenne::serialization::msgpack large string and array round trip") {
  SUBCASE("4 KiB string via str16") {
    auto const s{std::string(4096, 'Q')};
    auto buf{std::vector<std::byte>(s.size() + 8)};
    auto w{msgpack::writer{std::span<std::byte>{buf}}};
    REQUIRE(w.write_string(s).has_value());
    CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xDA);  // str16

    auto r{msgpack::reader{w.written()}};
    auto const out{r.read_string()};
    REQUIRE(out.has_value());
    CHECK(out->size() == 4096);
    CHECK(*out == s);
  }
  SUBCASE("array of 1000 integers") {
    auto buf{std::vector<std::byte>(4096)};
    auto w{msgpack::writer{std::span<std::byte>{buf}}};
    REQUIRE(w.write_array_header(1000).has_value());
    for (int i{0}; i < 1000; ++i)
      REQUIRE(w.write_int(i).has_value());

    auto r{msgpack::reader{w.written()}};
    REQUIRE(*r.read_array_header() == 1000u);
    for (int i{0}; i < 1000; ++i)
      CHECK(*r.read_int() == i);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack read_int accepts non-canonical wide encodings of 1") {
  // The value 1 in every integer width the reader supports.
  auto const cases{std::array<std::vector<std::byte>, 7>{{
    bytes({0x01}),                             // positive fixint
    bytes({0xCC, 0x01}),                       // uint8
    bytes({0xCD, 0x00, 0x01}),                 // uint16
    bytes({0xCE, 0x00, 0x00, 0x00, 0x01}),     // uint32
    bytes({0xCF, 0, 0, 0, 0, 0, 0, 0, 0x01}),  // uint64
    bytes({0xD0, 0x01}),                       // int8
    bytes({0xD3, 0, 0, 0, 0, 0, 0, 0, 0x01}),  // int64
  }}};
  for (auto const& wire : cases) {
    auto r{msgpack::reader{as_span(wire)}};
    auto const got{r.read_int()};
    REQUIRE(got.has_value());
    CHECK(*got == 1);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack read_int sign-extends wide negative encodings of -1") {
  auto const cases{std::array<std::vector<std::byte>, 5>{{
    bytes({0xFF}),                                                  // negative fixint
    bytes({0xD0, 0xFF}),                                            // int8
    bytes({0xD1, 0xFF, 0xFF}),                                      // int16
    bytes({0xD2, 0xFF, 0xFF, 0xFF, 0xFF}),                          // int32
    bytes({0xD3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}),  // int64
  }}};
  for (auto const& wire : cases) {
    auto r{msgpack::reader{as_span(wire)}};
    CHECK(*r.read_int() == -1);
    CHECK(r.at_end());
  }
}

TEST_CASE("nexenne::serialization::msgpack uint64 above INT64_MAX wraps on read but keeps bits") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{msgpack::writer{buf}};
  auto const big{0xFFFFFFFFFFFFFFFFULL};
  REQUIRE(w.write_uint(big).has_value());
  CHECK(static_cast<std::uint8_t>(w.written()[0]) == 0xCF);

  auto r{msgpack::reader{w.written()}};
  auto const got{r.read_int()};
  REQUIRE(got.has_value());
  CHECK(static_cast<std::uint64_t>(*got) == big);  // -1 reinterpreted == all ones
}

}  // namespace
