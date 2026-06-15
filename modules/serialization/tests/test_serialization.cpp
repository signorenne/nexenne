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

TEST_CASE("json::value - type queries") {
  CHECK(json::value{}.is_null());
  CHECK(json::value{true}.is_bool());
  CHECK(json::value{42}.is_integer());
  CHECK(json::value{3.14}.is_floating());
  CHECK(json::value{42}.is_number());
  CHECK(json::value{3.14}.is_number());
  CHECK(json::value{"hi"}.is_string());
  CHECK(json::value{json::array{1, 2, 3}}.is_array());
  CHECK(json::value{json::object{{"k", 1}}}.is_object());
}

TEST_CASE("json::value - typed access") {
  auto const v{json::value{42}};
  CHECK(*v.as_int() == 42);
  CHECK(*v.as_float() == 42.0);
  CHECK(!v.as_string().has_value());
  CHECK(v.get<int>() == 42);
  CHECK(v.get<double>() == 42.0);
  CHECK(v.get_or<int>(0) == 42);
  CHECK(v.get_or<std::string>("x") == "x");
}

TEST_CASE("json::value - operator[] builder syntax") {
  auto root{json::value{}};
  root["name"] = "alice";
  root["age"] = 30;
  root["admin"] = true;
  CHECK(root.is_object());
  CHECK(*root["name"].as_string() == "alice");
  CHECK(*root["age"].as_int() == 30);
  CHECK(*root["admin"].as_bool() == true);
}

TEST_CASE("json::value - at_path RFC 6901") {
  auto root{json::value{json::object{
    {"users",
     json::array{
       json::object{{"name", "alice"}},
       json::object{{"name", "bob"}},
     }},
    {"a/b", "slash"},
    {"a~b", "tilde"},
  }}};
  CHECK(*root.at_path("/users/0/name")->get().as_string() == "alice");
  CHECK(*root.at_path("/users/1/name")->get().as_string() == "bob");
  CHECK(*root.at_path("/a~1b")->get().as_string() == "slash");
  CHECK(*root.at_path("/a~0b")->get().as_string() == "tilde");
  CHECK(!root.at_path("/missing").has_value());
  CHECK(!root.at_path("/users/99/name").has_value());
}

TEST_CASE("json::parse - primitives") {
  CHECK(json::parse("null")->is_null());
  CHECK(*json::parse("true")->as_bool() == true);
  CHECK(*json::parse("false")->as_bool() == false);
  CHECK(*json::parse("42")->as_int() == 42);
  CHECK(*json::parse("-17")->as_int() == -17);
  CHECK(*json::parse("3.14")->as_float() == 3.14);
  CHECK(*json::parse("1e3")->as_float() == 1000.0);
  CHECK(*json::parse("\"hi\"")->as_string() == "hi");
}

TEST_CASE("json::parse - string escapes") {
  CHECK(*json::parse(R"("a\nb")")->as_string() == "a\nb");
  CHECK(*json::parse(R"("\"x\"")")->as_string() == "\"x\"");
  CHECK(*json::parse(R"("é")")->as_string() == "\xc3\xa9");
}

TEST_CASE("json::parse - array and object") {
  auto const arr{json::parse("[1, 2, 3]")};
  REQUIRE(arr.has_value());
  REQUIRE(arr->is_array());
  CHECK(*(*arr)[0].as_int() == 1);
  CHECK(*(*arr)[2].as_int() == 3);

  auto const obj{json::parse(R"({"a":1,"b":"x"})")};
  REQUIRE(obj.has_value());
  REQUIRE(obj->is_object());
  CHECK(*(*obj)["a"].as_int() == 1);
  CHECK(*(*obj)["b"].as_string() == "x");
}

TEST_CASE("json::parse - errors") {
  CHECK(!json::parse("nope").has_value());
  CHECK(!json::parse("{").has_value());
  CHECK(!json::parse("[1,2,]").has_value());
  CHECK(!json::parse(R"({"a":1,)").has_value());
  CHECK(!json::parse(R"({"a":1,"a":2})").has_value());  // duplicate key
}

TEST_CASE("json::parse - options") {
  auto opts{json::parse_options{}};
  opts.allow_trailing_commas = true;
  opts.allow_comments = true;
  CHECK(json::parse("[1,2,]", opts).has_value());
  CHECK(json::parse("[1 /* x */, 2]", opts).has_value());
  CHECK(json::parse("[1, // tail\n 2]", opts).has_value());

  auto deep{std::string{}};
  for (auto i{0}; i < 100; ++i)
    deep += '[';
  for (auto i{0}; i < 100; ++i)
    deep += ']';
  auto shallow{json::parse_options{.max_depth = 10}};
  CHECK(!json::parse(deep, shallow).has_value());
}

TEST_CASE("json::serialize - round trip") {
  auto const src{R"({"a":1,"b":[1,2,3],"c":{"d":true}})"};
  auto const parsed{json::parse(src)};
  REQUIRE(parsed.has_value());
  CHECK(json::serialize(*parsed) == src);
}

TEST_CASE("json::serialize - pretty") {
  auto const v{json::value{json::object{
    {"a", 1},
    {"b", json::array{1, 2}},
  }}};
  auto const pretty{json::serialize_pretty(v)};
  CHECK(pretty.find('\n') != std::string::npos);
  CHECK(pretty.find("  ") != std::string::npos);
}

TEST_CASE("json::serialize - string escapes") {
  auto const v{json::value{"a\nb\"c"}};
  CHECK(json::serialize(v) == R"("a\nb\"c")");
}

TEST_CASE("binary::writer + reader - primitives round trip") {
  auto buf{std::array<std::byte, 256>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{0xDEADBEEF}).has_value());
  REQUIRE(w.write(std::int64_t{-42}).has_value());
  REQUIRE(w.write(3.14).has_value());
  REQUIRE(w.write(std::string_view{"hello"}).has_value());

  auto const written{std::span{buf.data(), w.bytes_written()}};
  auto r{binary::reader{written}};
  CHECK(*r.read<std::uint32_t>() == 0xDEADBEEF);
  CHECK(*r.read<std::int64_t>() == -42);
  CHECK(*r.read<double>() == 3.14);
  CHECK(*r.read_string() == "hello");
  CHECK(r.at_end());
}

TEST_CASE("binary::writer - buffer_full") {
  auto buf{std::array<std::byte, 3>{}};
  auto w{binary::writer{buf}};
  auto const r{w.write(std::uint32_t{1})};
  CHECK(!r.has_value());
  CHECK(r.error() == error::buffer_full);
}

TEST_CASE("binary::reader - buffer_underrun") {
  auto buf{std::array<std::byte, 2>{}};
  auto r{binary::reader{std::span<std::byte const>{buf}}};
  auto const v{r.read<std::uint32_t>()};
  CHECK(!v.has_value());
  CHECK(v.error() == error::buffer_underrun);
}

TEST_CASE("binary - varint encoding") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_varint(0).has_value());
  REQUIRE(w.write_varint(127).has_value());
  REQUIRE(w.write_varint(128).has_value());
  REQUIRE(w.write_varint(0xFFFFFFFF).has_value());
  // 1 + 1 + 2 + 5 = 9 bytes.
  CHECK(w.bytes_written() == 9);

  auto r{binary::reader{std::span<std::byte const>{buf.data(), w.bytes_written()}}};
  CHECK(*r.read_varint() == 0);
  CHECK(*r.read_varint() == 127);
  CHECK(*r.read_varint() == 128);
  CHECK(*r.read_varint() == 0xFFFFFFFFu);
}

TEST_CASE("binary - strings of varying length") {
  auto buf{std::array<std::byte, 4096>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::string_view{""}).has_value());
  REQUIRE(w.write(std::string_view{"x"}).has_value());
  auto big{std::string(1000, 'q')};
  REQUIRE(w.write(std::string_view{big}).has_value());

  auto r{binary::reader{std::span<std::byte const>{buf.data(), w.bytes_written()}}};
  CHECK(*r.read_string() == "");
  CHECK(*r.read_string() == "x");
  CHECK(*r.read_string() == big);
}

TEST_CASE("binary - zigzag signed integers") {
  auto buf{std::array<std::byte, 64>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_zigzag(0).has_value());
  REQUIRE(w.write_zigzag(-1).has_value());
  REQUIRE(w.write_zigzag(63).has_value());
  REQUIRE(w.write_zigzag(-64).has_value());
  REQUIRE(w.write_zigzag(std::int64_t{-1'000'000'000}).has_value());

  auto r{binary::reader{std::span<std::byte const>{buf.data(), w.bytes_written()}}};
  CHECK(*r.read_zigzag() == 0);
  CHECK(*r.read_zigzag() == -1);
  CHECK(*r.read_zigzag() == 63);
  CHECK(*r.read_zigzag() == -64);
  CHECK(*r.read_zigzag() == -1'000'000'000);
}

TEST_CASE("binary - array bulk read/write") {
  auto buf{std::array<std::byte, 256>{}};
  auto const src{std::array<std::uint32_t, 4>{0xAA, 0xBB, 0xCC, 0xDD}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write_array(std::span<std::uint32_t const>{src}).has_value());
  CHECK(w.bytes_written() == 16);

  auto dst{std::array<std::uint32_t, 4>{}};
  auto r{binary::reader{std::span<std::byte const>{buf.data(), w.bytes_written()}}};
  REQUIRE(r.read_array(std::span<std::uint32_t>{dst}).has_value());
  CHECK(dst == src);
}

TEST_CASE("binary - peek / skip / seek") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{0x11223344}).has_value());
  REQUIRE(w.write(std::uint16_t{0x5566}).has_value());

  auto r{binary::reader{std::span<std::byte const>{buf.data(), w.bytes_written()}}};
  CHECK(*r.peek<std::uint32_t>() == 0x11223344);
  CHECK(r.position() == 0);  // peek didn't advance
  REQUIRE(r.skip(4).has_value());
  CHECK(*r.read<std::uint16_t>() == 0x5566);
  REQUIRE(r.seek(0).has_value());
  CHECK(*r.read<std::uint32_t>() == 0x11223344);
}

TEST_CASE("binary - writer reset reuses buffer") {
  auto buf{std::array<std::byte, 32>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{42}).has_value());
  CHECK(w.bytes_written() == 4);
  w.reset();
  CHECK(w.bytes_written() == 0);
  REQUIRE(w.write(std::uint64_t{100}).has_value());
  CHECK(w.bytes_written() == 8);
}

struct counting_visitor : json::noop_visitor {
  int nulls{0}, bools{0}, ints{0}, floats{0}, strings{0};
  int begin_objs{0}, end_objs{0}, begin_arrs{0}, end_arrs{0};
  int keys{0};
  std::int64_t last_int{0};
  std::string last_key{}, last_string{};

  auto on_null() noexcept -> bool {
    ++nulls;
    return true;
  }

  auto on_bool(bool) noexcept -> bool {
    ++bools;
    return true;
  }

  auto on_int(std::int64_t i) noexcept -> bool {
    ++ints;
    last_int = i;
    return true;
  }

  auto on_float(double) noexcept -> bool {
    ++floats;
    return true;
  }

  auto on_string(std::string_view s) noexcept -> bool {
    ++strings;
    last_string = s;
    return true;
  }

  auto on_key(std::string_view s) noexcept -> bool {
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

TEST_CASE("json::scan - primitives and structure") {
  auto v{counting_visitor{}};
  REQUIRE(json::scan(R"({"a":1,"b":[true,null,2.5,"x"]})", v).has_value());
  CHECK(v.begin_objs == 1);
  CHECK(v.end_objs == 1);
  CHECK(v.begin_arrs == 1);
  CHECK(v.end_arrs == 1);
  CHECK(v.keys == 2);
  CHECK(v.ints == 1);
  CHECK(v.floats == 1);
  CHECK(v.bools == 1);
  CHECK(v.nulls == 1);
  CHECK(v.strings == 1);
}

TEST_CASE("json::scan - visitor abort short-circuits") {
  struct abort_on_key : json::noop_visitor {
    std::string captured;

    auto on_key(std::string_view k) noexcept -> bool {
      captured = k;
      return false;
    }
  };

  auto v{abort_on_key{}};
  auto const r{json::scan(R"({"target":42,"unused":99})", v)};
  CHECK(!r.has_value());
  CHECK(r.error() == error::invalid_input);
  CHECK(v.captured == "target");
}

TEST_CASE("json::scan - depth limit") {
  auto v{counting_visitor{}};
  auto const r{json::scan<4>("[[[[[1]]]]]", v)};
  CHECK(!r.has_value());
  CHECK(r.error() == error::depth_limit_exceeded);
}

TEST_CASE("json::scan - zero allocation contract (compile-time)") {
  // The engine carries only a string_view, a size_t cursor, a
  // bounded depth stack, and a depth counter. Verify this stays
  // small enough to fit comfortably on an MCU stack.
  static_assert(
    sizeof(json::detail::sax_engine<32>) <= 128,
    "SAX engine grew unexpectedly - review MCU footprint"
  );
  CHECK(true);
}

TEST_CASE("json::writer - flat object") {
  auto buf{std::array<char, 256>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.begin_object().has_value());
  REQUIRE(w.key("name").has_value());
  REQUIRE(w.value("alice").has_value());
  REQUIRE(w.key("age").has_value());
  REQUIRE(w.value(30).has_value());
  REQUIRE(w.key("admin").has_value());
  REQUIRE(w.value(true).has_value());
  REQUIRE(w.end_object().has_value());
  CHECK(w.is_complete());
  CHECK(w.view() == R"({"name":"alice","age":30,"admin":true})");
}

TEST_CASE("json::writer - nested") {
  auto buf{std::array<char, 256>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.begin_object().has_value());
  REQUIRE(w.key("xs").has_value());
  REQUIRE(w.begin_array().has_value());
  REQUIRE(w.value(1).has_value());
  REQUIRE(w.value(2).has_value());
  REQUIRE(w.value(3).has_value());
  REQUIRE(w.end_array().has_value());
  REQUIRE(w.key("nested").has_value());
  REQUIRE(w.begin_object().has_value());
  REQUIRE(w.key("k").has_value());
  REQUIRE(w.value_null().has_value());
  REQUIRE(w.end_object().has_value());
  REQUIRE(w.end_object().has_value());
  CHECK(w.is_complete());
  CHECK(w.view() == R"({"xs":[1,2,3],"nested":{"k":null}})");
}

TEST_CASE("json::writer - escapes") {
  auto buf{std::array<char, 64>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.value("a\nb\"c").has_value());
  CHECK(w.view() == R"("a\nb\"c")");
}

TEST_CASE("json::writer - buffer_full") {
  auto buf{std::array<char, 4>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.begin_object().has_value());
  auto const r{w.key("long_key")};
  CHECK(!r.has_value());
  CHECK(r.error() == error::buffer_full);
}

TEST_CASE("json::writer - structural errors") {
  auto buf{std::array<char, 64>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.begin_array().has_value());
  // key() in an array context is invalid
  auto const r1{w.key("nope")};
  CHECK(!r1.has_value());
  CHECK(r1.error() == error::invalid_input);
  // end_object() with array open is invalid
  auto const r2{w.end_object()};
  CHECK(!r2.has_value());
  CHECK(r2.error() == error::invalid_input);
}

TEST_CASE("json::writer + json::scan - round trip on embedded path") {
  auto buf{std::array<char, 256>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.begin_object().has_value());
  REQUIRE(w.key("id").has_value());
  REQUIRE(w.value(42).has_value());
  REQUIRE(w.key("temp").has_value());
  REQUIRE(w.value(23.5).has_value());
  REQUIRE(w.key("on").has_value());
  REQUIRE(w.value(true).has_value());
  REQUIRE(w.end_object().has_value());

  auto v{counting_visitor{}};
  REQUIRE(json::scan(w.view(), v).has_value());
  CHECK(v.keys == 3);
  CHECK(v.ints == 1);
  CHECK(v.floats == 1);
  CHECK(v.bools == 1);
}

TEST_CASE("msgpack - round trip primitives") {
  std::array<std::byte, 128> buf{};
  msgpack::writer w{buf};
  REQUIRE(w.write_nil().has_value());
  REQUIRE(w.write_bool(true).has_value());
  REQUIRE(w.write_int(-1).has_value());
  REQUIRE(w.write_int(42).has_value());
  REQUIRE(w.write_int(-1000).has_value());
  REQUIRE(w.write_uint(123456789).has_value());
  REQUIRE(w.write_float64(3.14).has_value());
  REQUIRE(w.write_string("hello").has_value());

  msgpack::reader r{w.written()};
  REQUIRE(r.read_nil().has_value());
  CHECK(*r.read_bool() == true);
  CHECK(*r.read_int() == -1);
  CHECK(*r.read_int() == 42);
  CHECK(*r.read_int() == -1000);
  CHECK(*r.read_int() == 123456789);
  CHECK(*r.read_float() == doctest::Approx(3.14));
  CHECK(*r.read_string() == "hello");
  CHECK(r.at_end());
}

TEST_CASE("msgpack - array + map headers") {
  std::array<std::byte, 64> buf{};
  msgpack::writer w{buf};
  REQUIRE(w.write_array_header(3).has_value());
  REQUIRE(w.write_int(1).has_value());
  REQUIRE(w.write_int(2).has_value());
  REQUIRE(w.write_int(3).has_value());
  REQUIRE(w.write_map_header(1).has_value());
  REQUIRE(w.write_string("k").has_value());
  REQUIRE(w.write_int(7).has_value());

  msgpack::reader r{w.written()};
  CHECK(*r.read_array_header() == 3u);
  CHECK(*r.read_int() == 1);
  CHECK(*r.read_int() == 2);
  CHECK(*r.read_int() == 3);
  CHECK(*r.read_map_header() == 1u);
  CHECK(*r.read_string() == "k");
  CHECK(*r.read_int() == 7);
}

TEST_CASE("cbor - round trip primitives") {
  std::array<std::byte, 128> buf{};
  cbor::writer w{buf};
  REQUIRE(w.write_null().has_value());
  REQUIRE(w.write_bool(false).has_value());
  REQUIRE(w.write_int(-1).has_value());
  REQUIRE(w.write_int(257).has_value());
  REQUIRE(w.write_uint(0xDEADBEEFu).has_value());
  REQUIRE(w.write_float64(2.71828).has_value());
  REQUIRE(w.write_string("cbor").has_value());

  cbor::reader r{w.written()};
  REQUIRE(r.read_null().has_value());
  CHECK(*r.read_bool() == false);
  CHECK(*r.read_int() == -1);
  CHECK(*r.read_int() == 257);
  CHECK(*r.read_uint() == 0xDEADBEEFu);
  CHECK(*r.read_float() == doctest::Approx(2.71828));
  CHECK(*r.read_string() == "cbor");
  CHECK(r.at_end());
}

TEST_CASE("cbor - array + map headers") {
  std::array<std::byte, 32> buf{};
  cbor::writer w{buf};
  REQUIRE(w.write_array_header(2).has_value());
  REQUIRE(w.write_int(10).has_value());
  REQUIRE(w.write_int(20).has_value());
  REQUIRE(w.write_map_header(1).has_value());
  REQUIRE(w.write_string("ok").has_value());
  REQUIRE(w.write_bool(true).has_value());

  cbor::reader r{w.written()};
  CHECK(*r.read_array_header() == 2u);
  CHECK(*r.read_int() == 10);
  CHECK(*r.read_int() == 20);
  CHECK(*r.read_map_header() == 1u);
  CHECK(*r.read_string() == "ok");
  CHECK(*r.read_bool() == true);
}

TEST_CASE("versioned - envelope round trip") {
  constexpr std::uint32_t magic{0x4E455845};
  std::array<std::byte, 64> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 7).has_value());
  REQUIRE(w.write(std::uint32_t{42}).has_value());

  binary::reader r{w.written()};
  auto h{read_header(r, magic)};
  REQUIRE(h.has_value());
  CHECK(h->magic == magic);
  CHECK(h->version == 7u);
  CHECK(*r.read<std::uint32_t>() == 42u);
}

TEST_CASE("versioned - wrong magic rejected") {
  constexpr std::uint32_t magic{0xCAFEBABE};
  std::array<std::byte, 16> buf{};
  binary::writer w{buf};
  REQUIRE(write_header(w, magic, 1).has_value());

  binary::reader r{w.written()};
  auto h{read_header(r, 0xDEADBEEF)};
  CHECK(!h.has_value());
  CHECK(h.error() == error::invalid_input);
}

// Regression tests for bugs found and fixed during the port (audit cases the
// original suite missed).

TEST_CASE("regression: as_int rejects an out-of-range double instead of UB") {
  auto const big{json::parse("1e300")};
  REQUIRE(big.has_value());
  REQUIRE(big->is_floating());
  CHECK_FALSE(big->as_int().has_value());  // type_mismatch, not undefined behaviour
  CHECK(big->as_int().error() == error::type_mismatch);
  CHECK(*json::value{3.9}.as_int() == 3);  // an in-range double still truncates
  CHECK(*json::value{-3.9}.as_int() == -3);
}

TEST_CASE("regression: JSON parser validates \\u surrogates") {
  // valid surrogate pair: U+1D11E (musical G clef)
  auto const pair{json::parse(R"("𝄞")")};
  REQUIRE(pair.has_value());
  CHECK(*pair->as_string() == "\xF0\x9D\x84\x9E");
  CHECK_FALSE(json::parse(R"("\uDC00")").has_value());   // lone low surrogate
  CHECK_FALSE(json::parse(R"("\uD800A")").has_value());  // high then non-low (would underflow)
  CHECK_FALSE(json::parse(R"("\uD800x")").has_value());  // lone high surrogate
}

TEST_CASE("regression: a large double round-trips through serialize/parse") {
  // to_chars renders this magnitude in pure-integer form; without a ".0" it
  // would reparse as an integer and overflow.
  auto const back{json::parse(json::serialize(json::value{3.148e19}))};
  REQUIRE(back.has_value());
  CHECK(back->is_floating());
  CHECK(*back->as_float() == 3.148e19);
  auto const two{json::parse(json::serialize(json::value{2.0}))};  // integer-valued double
  REQUIRE(two.has_value());
  CHECK(two->is_floating());
}

TEST_CASE("regression: parser rejects non-RFC-8259 number forms") {
  CHECK_FALSE(json::parse("01").has_value());  // leading zero
  CHECK_FALSE(json::parse("00").has_value());
  CHECK_FALSE(json::parse("-01").has_value());
  CHECK_FALSE(json::parse("1.").has_value());  // no digit after '.'
  CHECK_FALSE(json::parse("1e").has_value());  // no digit in exponent
  CHECK_FALSE(json::parse("1e+").has_value());
  CHECK(*json::parse("0")->as_int() == 0);  // valid forms still parse
  CHECK(*json::parse("-0")->as_int() == 0);
  CHECK(*json::parse("10")->as_int() == 10);
  CHECK(*json::parse("1.5")->as_float() == 1.5);
  CHECK(*json::parse("1e3")->as_float() == 1000.0);
}

TEST_CASE("regression: at_path rejects malformed array indices") {
  auto const root{json::parse("[10,20,30]")};
  REQUIRE(root.has_value());
  CHECK(*root->at_path("/0")->get().as_int() == 10);
  CHECK_FALSE(root->at_path("/01").has_value());                    // leading zero
  CHECK_FALSE(root->at_path("/18446744073709551618").has_value());  // overflows size_t
  CHECK_FALSE(root->at_path("/99").has_value());                    // out of range
}

TEST_CASE("regression: CBOR decodes subnormal half-floats correctly") {
  // f9 00 01 = smallest positive subnormal half = 2^-24
  std::array<std::byte, 3> const smallest{std::byte{0xF9}, std::byte{0x00}, std::byte{0x01}};
  cbor::reader r{std::span<std::byte const>{smallest}};
  auto const d{r.read_float()};
  REQUIRE(d.has_value());
  CHECK(*d == doctest::Approx(5.9604644775390625e-08));  // 2^-24, not a quarter of it
}

TEST_CASE("regression: parser enforces max_depth without overflowing the stack") {
  auto deep{std::string(200, '[')};
  deep += std::string(200, ']');
  auto const r{json::parse(deep)};  // 200 > default max_depth (128)
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == error::depth_limit_exceeded);
  auto ok{std::string(50, '[')};  // a moderate depth within the limit parses
  ok += std::string(50, ']');
  CHECK(json::parse(ok).has_value());
}

TEST_CASE("regression: an over-large integer literal parses as a double, not an error") {
  auto const r{json::parse("99999999999999999999")};  // beyond int64 range
  REQUIRE(r.has_value());
  CHECK(r->is_floating());
  CHECK(*r->as_float() == doctest::Approx(1e20));
}

TEST_CASE("regression: an unterminated block comment is rejected") {
  auto opts{json::parse_options{}};
  opts.allow_comments = true;
  CHECK_FALSE(json::parse("42 /* unterminated", opts).has_value());
  CHECK(json::parse("42 /* closed */", opts).has_value());  // a terminated comment is fine
}

TEST_CASE("regression: a deeply nested value is torn down without a stack overflow") {
  // Build a DOM by hand far past the parser's max_depth; the iterative
  // destructor must release it without unbounded recursion (a recursive
  // destructor segfaults around this depth).
  auto v{json::value{json::array{}}};
  auto* cur{&v};
  for (auto i{0}; i < 50000; ++i) {
    cur->array_mut().push_back(json::value{json::array{}});
    cur = &cur->array_mut()[0];
  }
  REQUIRE(v.is_array());
  auto moved{std::move(v)};  // move is O(1), no deep recursion
  CHECK(moved.is_array());
  // moved destructs here at depth 50000 without overflowing the stack.
}

}  // namespace
