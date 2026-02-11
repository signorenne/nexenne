#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <nexenne/serialization/serialization.hpp>

namespace {

using namespace nexenne::serialization;

TEST_CASE("binary::reader - peek does not advance and matches read") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint16_t{0xBEEF}).has_value());
  REQUIRE(w.write(std::uint8_t{0x7F}).has_value());

  auto r{binary::reader{w.written()}};
  auto const p1{r.peek<std::uint16_t>()};
  REQUIRE(p1.has_value());
  CHECK(*p1 == 0xBEEF);
  CHECK(r.position() == 0);  // peek must leave the cursor where it was
  CHECK(*r.read<std::uint16_t>() == 0xBEEF);
  CHECK(r.position() == 2);  // read advances
  CHECK(*r.peek<std::uint8_t>() == 0x7F);
  CHECK(r.position() == 2);
}

TEST_CASE("binary::reader - peek and read report buffer_underrun symmetrically") {
  auto buf{std::array<std::byte, 2>{}};
  auto r{binary::reader{std::span<std::byte const>{buf}}};
  auto const pk{r.peek<std::uint32_t>()};
  CHECK(!pk.has_value());
  CHECK(pk.error() == error::buffer_underrun);
  CHECK(r.position() == 0);  // failed peek leaves cursor untouched
}

TEST_CASE("binary::reader - skip advances and is bounds checked") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{0x01020304}).has_value());
  REQUIRE(w.write(std::uint32_t{0xAABBCCDD}).has_value());

  auto r{binary::reader{w.written()}};
  REQUIRE(r.skip(4).has_value());
  CHECK(r.position() == 4);
  CHECK(*r.read<std::uint32_t>() == 0xAABBCCDD);
  CHECK(r.at_end());

  auto const over{r.skip(1)};  // nothing left to skip
  CHECK(!over.has_value());
  CHECK(over.error() == error::buffer_underrun);
  CHECK(r.position() == 8);  // failed skip does not move the cursor
}

TEST_CASE("binary::reader - set_max_string_size rejects oversize string") {
  auto buf{std::array<std::byte, 64>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::string_view{"abcdef"}).has_value());  // 6-byte body

  SUBCASE("cap below body length is rejected") {
    auto r{binary::reader{w.written()}};
    r.set_max_string_size(3);
    auto const s{r.read_string()};
    CHECK(!s.has_value());
    CHECK(s.error() == error::string_too_long);
  }
  SUBCASE("cap at or above body length succeeds") {
    auto r{binary::reader{w.written()}};
    r.set_max_string_size(6);
    auto const s{r.read_string()};
    REQUIRE(s.has_value());
    CHECK(*s == "abcdef");
  }
}

TEST_CASE("cbor - unsigned and negative integer major types") {
  auto buf{std::array<std::byte, 64>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_uint(0).has_value());
  REQUIRE(w.write_uint(23).has_value());   // single-byte form
  REQUIRE(w.write_uint(24).has_value());   // 1-byte argument form
  REQUIRE(w.write_uint(300).has_value());  // 2-byte argument form
  REQUIRE(w.write_int(-1).has_value());    // major type 1
  REQUIRE(w.write_int(-500).has_value());

  auto r{cbor::reader{w.written()}};

  REQUIRE(r.peek_type().has_value());
  CHECK(*r.peek_type() == cbor::type::unsigned_int);
  CHECK(*r.read_uint() == 0u);
  CHECK(*r.read_uint() == 23u);
  CHECK(*r.read_uint() == 24u);
  CHECK(*r.read_uint() == 300u);

  REQUIRE(r.peek_type().has_value());
  CHECK(*r.peek_type() == cbor::type::negative_int);
  CHECK(*r.read_int() == -1);
  CHECK(*r.read_int() == -500);
  CHECK(r.at_end());
}

TEST_CASE("cbor - read_uint rejects a negative item with type_mismatch") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_int(-7).has_value());

  auto r{cbor::reader{w.written()}};
  auto const u{r.read_uint()};
  CHECK(!u.has_value());
  CHECK(u.error() == error::type_mismatch);
}

TEST_CASE("cbor - byte string and text string major types") {
  auto buf{std::array<std::byte, 64>{}};
  auto const payload{std::array<std::byte, 3>{std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_bytes(std::span<std::byte const>{payload}).has_value());
  REQUIRE(w.write_string("text\xC3\xA9").has_value());

  auto r{cbor::reader{w.written()}};
  CHECK(*r.peek_type() == cbor::type::byte_string);
  auto const bs{r.read_bytes()};
  REQUIRE(bs.has_value());
  REQUIRE(bs->size() == 3);
  CHECK((*bs)[0] == std::byte{0x01});
  CHECK((*bs)[2] == std::byte{0xFF});

  CHECK(*r.peek_type() == cbor::type::text_string);
  auto const ts{r.read_string()};
  REQUIRE(ts.has_value());
  CHECK(*ts == "text\xC3\xA9");
  CHECK(r.at_end());
}

TEST_CASE("cbor - array and map major types round trip") {
  auto buf{std::array<std::byte, 64>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_array_header(3).has_value());
  REQUIRE(w.write_int(7).has_value());
  REQUIRE(w.write_int(-2).has_value());
  REQUIRE(w.write_string("z").has_value());
  REQUIRE(w.write_map_header(2).has_value());
  REQUIRE(w.write_string("k1").has_value());
  REQUIRE(w.write_uint(1).has_value());
  REQUIRE(w.write_string("k2").has_value());
  REQUIRE(w.write_bool(false).has_value());

  auto r{cbor::reader{w.written()}};
  CHECK(*r.peek_type() == cbor::type::array_header);
  CHECK(*r.read_array_header() == 3u);
  CHECK(*r.read_int() == 7);
  CHECK(*r.read_int() == -2);
  CHECK(*r.read_string() == "z");
  CHECK(*r.peek_type() == cbor::type::map_header);
  CHECK(*r.read_map_header() == 2u);
  CHECK(*r.read_string() == "k1");
  CHECK(*r.read_uint() == 1u);
  CHECK(*r.read_string() == "k2");
  CHECK(*r.read_bool() == false);
  CHECK(r.at_end());
}

TEST_CASE("cbor - bool, null and float simple values round trip") {
  auto buf{std::array<std::byte, 64>{}};
  auto w{cbor::writer{buf}};
  REQUIRE(w.write_bool(true).has_value());
  REQUIRE(w.write_null().has_value());
  REQUIRE(w.write_float32(1.5F).has_value());  // exactly representable
  REQUIRE(w.write_float64(3.14159).has_value());

  auto r{cbor::reader{w.written()}};
  CHECK(*r.peek_type() == cbor::type::boolean);
  CHECK(*r.read_bool() == true);
  CHECK(*r.peek_type() == cbor::type::null);
  REQUIRE(r.read_null().has_value());
  CHECK(*r.peek_type() == cbor::type::floating);
  CHECK(*r.read_float() == doctest::Approx(1.5));
  CHECK(*r.read_float() == doctest::Approx(3.14159));
  CHECK(r.at_end());
}

TEST_CASE("cbor - peek_type rejects an unsupported tag byte") {
  // 0xC0 is major type 6 (tags), which the reader does not support.
  auto const raw{std::array<std::byte, 1>{std::byte{0xC0}}};
  auto r{cbor::reader{std::span<std::byte const>{raw}}};
  auto const t{r.peek_type()};
  CHECK(!t.has_value());
  CHECK(t.error() == error::invalid_input);
}

TEST_CASE("msgpack - integer size variants round trip") {
  auto buf{std::array<std::byte, 128>{}};
  auto w{msgpack::writer{buf}};
  // Positive fixint (<= 0x7F), then each widening boundary.
  REQUIRE(w.write_int(0).has_value());
  REQUIRE(w.write_int(127).has_value());                      // positive fixint
  REQUIRE(w.write_uint(255).has_value());                     // uint8
  REQUIRE(w.write_uint(65535).has_value());                   // uint16
  REQUIRE(w.write_uint(4294967295u).has_value());             // uint32
  REQUIRE(w.write_uint(std::uint64_t{1} << 40).has_value());  // uint64
  // Negative ladder.
  REQUIRE(w.write_int(-1).has_value());                        // negative fixint
  REQUIRE(w.write_int(-32).has_value());                       // negative fixint edge
  REQUIRE(w.write_int(-128).has_value());                      // int8
  REQUIRE(w.write_int(-32768).has_value());                    // int16
  REQUIRE(w.write_int(-2147483648LL).has_value());             // int32
  REQUIRE(w.write_int(-(std::int64_t{1} << 40)).has_value());  // int64

  auto r{msgpack::reader{w.written()}};
  CHECK(*r.peek_type() == msgpack::type::integer);
  CHECK(*r.read_int() == 0);
  CHECK(*r.read_int() == 127);
  CHECK(*r.read_int() == 255);
  CHECK(*r.read_int() == 65535);
  CHECK(*r.read_int() == 4294967295LL);
  CHECK(*r.read_int() == (std::int64_t{1} << 40));
  CHECK(*r.read_int() == -1);
  CHECK(*r.read_int() == -32);
  CHECK(*r.read_int() == -128);
  CHECK(*r.read_int() == -32768);
  CHECK(*r.read_int() == -2147483648LL);
  CHECK(*r.read_int() == -(std::int64_t{1} << 40));
  CHECK(r.at_end());
}

TEST_CASE("msgpack - read_int rejects a non-integer with type_mismatch") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{msgpack::writer{buf}};
  REQUIRE(w.write_string("nope").has_value());

  auto r{msgpack::reader{w.written()}};
  CHECK(*r.peek_type() == msgpack::type::string);
  auto const i{r.read_int()};
  CHECK(!i.has_value());
  CHECK(i.error() == error::type_mismatch);
}

TEST_CASE("msgpack - array of mixed values round trip") {
  auto buf{std::array<std::byte, 128>{}};
  auto w{msgpack::writer{buf}};
  REQUIRE(w.write_array_header(4).has_value());
  REQUIRE(w.write_int(-5).has_value());
  REQUIRE(w.write_bool(true).has_value());
  REQUIRE(w.write_string("ok").has_value());
  REQUIRE(w.write_nil().has_value());

  auto r{msgpack::reader{w.written()}};
  CHECK(*r.peek_type() == msgpack::type::array_header);
  CHECK(*r.read_array_header() == 4u);
  CHECK(*r.read_int() == -5);
  CHECK(*r.read_bool() == true);
  CHECK(*r.read_string() == "ok");
  REQUIRE(r.read_nil().has_value());
  CHECK(r.at_end());
}

TEST_CASE("msgpack - map round trip") {
  auto buf{std::array<std::byte, 128>{}};
  auto w{msgpack::writer{buf}};
  REQUIRE(w.write_map_header(2).has_value());
  REQUIRE(w.write_string("id").has_value());
  REQUIRE(w.write_uint(42).has_value());
  REQUIRE(w.write_string("temp").has_value());
  REQUIRE(w.write_float64(23.5).has_value());

  auto r{msgpack::reader{w.written()}};
  CHECK(*r.peek_type() == msgpack::type::map_header);
  CHECK(*r.read_map_header() == 2u);
  CHECK(*r.read_string() == "id");
  CHECK(*r.read_int() == 42);
  CHECK(*r.read_string() == "temp");
  CHECK(*r.peek_type() == msgpack::type::floating);
  CHECK(*r.read_float() == doctest::Approx(23.5));
  CHECK(r.at_end());
}

TEST_CASE("msgpack - binary blob round trip") {
  auto buf{std::array<std::byte, 64>{}};
  auto const blob{
    std::array<std::byte, 4>{std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}}
  };
  auto w{msgpack::writer{buf}};
  REQUIRE(w.write_binary(std::span<std::byte const>{blob}).has_value());

  auto r{msgpack::reader{w.written()}};
  CHECK(*r.peek_type() == msgpack::type::binary);
  auto const out{r.read_binary()};
  REQUIRE(out.has_value());
  REQUIRE(out->size() == 4);
  CHECK((*out)[0] == std::byte{0xDE});
  CHECK((*out)[3] == std::byte{0xEF});
  CHECK(r.at_end());
}

struct recording_visitor : json::noop_visitor {
  int nulls{0}, bools{0}, ints{0}, floats{0}, strings{0}, keys{0};
  int begin_objs{0}, end_objs{0}, begin_arrs{0}, end_arrs{0};
  bool last_bool{false};
  std::int64_t last_int{0};
  double last_float{0.0};
  std::string last_string{};
  std::string last_key{};

  auto on_null() noexcept -> bool {
    ++nulls;
    return true;
  }

  auto on_bool(bool const b) noexcept -> bool {
    ++bools;
    last_bool = b;
    return true;
  }

  auto on_int(std::int64_t const i) noexcept -> bool {
    ++ints;
    last_int = i;
    return true;
  }

  auto on_float(double const d) noexcept -> bool {
    ++floats;
    last_float = d;
    return true;
  }

  auto on_string(std::string_view const s) noexcept -> bool {
    ++strings;
    last_string = s;
    return true;
  }

  auto on_key(std::string_view const s) noexcept -> bool {
    ++keys;
    last_key = s;
    return true;
  }

  auto on_begin_object() noexcept -> bool {
    ++begin_objs;
    return true;
  }

  auto on_end_object() noexcept -> bool {
    ++end_objs;
    return true;
  }

  auto on_begin_array() noexcept -> bool {
    ++begin_arrs;
    return true;
  }

  auto on_end_array() noexcept -> bool {
    ++end_arrs;
    return true;
  }
};

TEST_CASE("json::scan - delivers the right callback per value type") {
  auto v{recording_visitor{}};
  REQUIRE(json::scan(R"([null, true, false, -9, 2.5, "hi"])", v).has_value());
  CHECK(v.begin_arrs == 1);
  CHECK(v.end_arrs == 1);
  CHECK(v.nulls == 1);
  CHECK(v.bools == 2);
  CHECK(v.last_bool == false);  // last boolean seen was false
  CHECK(v.ints == 1);
  CHECK(v.last_int == -9);
  CHECK(v.floats == 1);
  CHECK(v.last_float == doctest::Approx(2.5));
  CHECK(v.strings == 1);
  CHECK(v.last_string == "hi");
}

TEST_CASE("json::scan - strings are passed raw and undecoded") {
  // The SAX engine does NOT decode escapes; the view spans the raw
  // source between the quotes, so the backslash escape survives verbatim.
  auto v{recording_visitor{}};
  REQUIRE(json::scan(R"(["a\nb\""])", v).has_value());
  CHECK(v.strings == 1);
  CHECK(v.last_string == R"(a\nb\")");  // 6 raw chars, not a newline
  CHECK(v.last_string.find('\n') == std::string::npos);
}

TEST_CASE("json::scan - object keys arrive in document order") {
  auto v{recording_visitor{}};
  REQUIRE(json::scan(R"({"first":1,"second":2})", v).has_value());
  CHECK(v.begin_objs == 1);
  CHECK(v.end_objs == 1);
  CHECK(v.keys == 2);
  CHECK(v.last_key == "second");
  CHECK(v.ints == 2);
}

TEST_CASE("json::scan - visitor abort on a value short-circuits the parse") {
  struct stop_on_int : json::noop_visitor {
    int seen{0};
    std::int64_t first{0};

    auto on_int(std::int64_t const i) noexcept -> bool {
      ++seen;
      first = i;
      return false;  // abort at the first integer
    }
  };

  auto v{stop_on_int{}};
  auto const r{json::scan(R"([1, 2, 3])", v)};
  CHECK(!r.has_value());
  CHECK(r.error() == error::invalid_input);
  CHECK(v.seen == 1);  // parsing stopped after the first int
  CHECK(v.first == 1);
}

TEST_CASE("json::scan - depth limit via MaxDepth template parameter") {
  auto v{recording_visitor{}};
  SUBCASE("nesting beyond the cap is rejected") {
    auto const r{json::scan<3>("[[[[1]]]]", v)};
    CHECK(!r.has_value());
    CHECK(r.error() == error::depth_limit_exceeded);
  }
  SUBCASE("nesting at the cap is accepted") {
    auto const r{json::scan<8>("[[[[1]]]]", v)};
    REQUIRE(r.has_value());
    CHECK(v.begin_arrs == 4);
    CHECK(v.end_arrs == 4);
    CHECK(v.ints == 1);
  }
}

TEST_CASE("json::scan - malformed input reports a parse error") {
  auto v{recording_visitor{}};
  auto const r{json::scan(R"({"a":})", v)};  // missing value
  CHECK(!r.has_value());
  CHECK((r.error() == error::unexpected_character || r.error() == error::unexpected_end));
}

struct sample {
  std::uint16_t version{};
  std::uint32_t payload{};
};

// A codec that dispatches on the envelope version. Version 1 reads a
// single u32 payload; version 2 reads two u16 halves and recombines.
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

TEST_CASE("versioned - decode_with dispatches to the v1 codec path") {
  constexpr std::uint32_t magic{0x4E455831};
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(write_header(w, magic, 1).has_value());
  REQUIRE(w.write(std::uint32_t{0xCAFEBABE}).has_value());

  auto r{binary::reader{w.written()}};
  auto const out{decode_with(r, magic, sample_codec{})};
  REQUIRE(out.has_value());
  CHECK(out->version == 1u);
  CHECK(out->payload == 0xCAFEBABEu);
}

TEST_CASE("versioned - decode_with dispatches to the v2 codec path") {
  constexpr std::uint32_t magic{0x4E455832};
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(write_header(w, magic, 2).has_value());
  REQUIRE(w.write(std::uint16_t{0xBABE}).has_value());  // lo
  REQUIRE(w.write(std::uint16_t{0xCAFE}).has_value());  // hi

  auto r{binary::reader{w.written()}};
  auto const out{decode_with(r, magic, sample_codec{})};
  REQUIRE(out.has_value());
  CHECK(out->version == 2u);
  CHECK(out->payload == 0xCAFEBABEu);  // hi << 16 | lo
}

TEST_CASE("versioned - decode_with rejects an unknown version through the codec") {
  constexpr std::uint32_t magic{0x4E455833};
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(write_header(w, magic, 99).has_value());
  REQUIRE(w.write(std::uint32_t{0}).has_value());

  auto r{binary::reader{w.written()}};
  auto const out{decode_with(r, magic, sample_codec{})};
  CHECK(!out.has_value());
  CHECK(out.error() == error::invalid_input);  // codec rejected version 99
}

TEST_CASE("versioned - decode_with rejects a wrong magic before calling the codec") {
  constexpr std::uint32_t stamped{0x11112222};
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(write_header(w, stamped, 1).has_value());
  REQUIRE(w.write(std::uint32_t{0x12345678}).has_value());

  auto r{binary::reader{w.written()}};
  auto const out{decode_with(r, 0x99998888, sample_codec{})};
  CHECK(!out.has_value());
  CHECK(out.error() == error::invalid_input);  // magic mismatch repackaged
}

TEST_CASE("versioned - decode_with reports buffer_underrun on a truncated envelope") {
  constexpr std::uint32_t magic{0x4E455834};
  // Only 4 bytes available - smaller than the 8-byte header.
  auto buf{std::array<std::byte, 4>{}};
  auto r{binary::reader{std::span<std::byte const>{buf}}};
  auto const out{decode_with(r, magic, sample_codec{})};
  CHECK(!out.has_value());
  CHECK(out.error() == error::buffer_underrun);
}

TEST_CASE("versioned - read_header surfaces the parsed version") {
  constexpr std::uint32_t magic{0x4E455835};
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(write_header(w, magic, 513).has_value());  // 0x0201, exercises both bytes

  auto r{binary::reader{w.written()}};
  auto const h{read_header(r, magic)};
  REQUIRE(h.has_value());
  CHECK(h->magic == magic);
  CHECK(h->version == 513u);
  CHECK(r.position() == versioned_header_size);
}

}  // namespace
