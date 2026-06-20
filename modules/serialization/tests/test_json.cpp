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

TEST_CASE("nexenne::serialization::json value - construction and type queries") {
  CHECK(json::value{}.is_null());
  CHECK(json::value{nullptr}.is_null());
  CHECK(json::value{true}.is_bool());
  CHECK(json::value{false}.is_bool());
  CHECK(json::value{42}.is_integer());
  CHECK(json::value{std::int64_t{-7}}.is_integer());
  CHECK(json::value{std::uint8_t{255}}.is_integer());
  CHECK(json::value{3.14}.is_floating());
  CHECK(json::value{3.14F}.is_floating());
  CHECK(json::value{"hi"}.is_string());
  CHECK(json::value{std::string{"hi"}}.is_string());
  CHECK(json::value{std::string_view{"hi"}}.is_string());
  CHECK(json::value{json::array{1, 2, 3}}.is_array());
  CHECK(json::value{json::object{{"k", 1}}}.is_object());

  CHECK(json::value{42}.is_number());
  CHECK(json::value{3.14}.is_number());
  CHECK_FALSE(json::value{"x"}.is_number());
  CHECK_FALSE(json::value{true}.is_number());

  // bool stays bool, does not become integer.
  CHECK(json::value{true}.type() == json::value::kind::boolean_kind);
  CHECK(json::value{42}.type() == json::value::kind::integer_kind);
  CHECK(json::value{1.0}.type() == json::value::kind::floating_kind);
  CHECK(json::value{}.type() == json::value::kind::null_kind);
  CHECK(json::value{"s"}.type() == json::value::kind::string_kind);
  CHECK(json::value{json::array{}}.type() == json::value::kind::array_kind);
  CHECK(json::value{json::object{}}.type() == json::value::kind::object_kind);
}

TEST_CASE("nexenne::serialization::json value - safe accessors return expected") {
  auto const i{json::value{42}};
  CHECK(*i.as_int() == 42);
  CHECK(*i.as_float() == 42.0);
  CHECK_FALSE(i.as_bool().has_value());
  CHECK(i.as_bool().error() == error::type_mismatch);
  CHECK_FALSE(i.as_string().has_value());
  CHECK(i.as_string().error() == error::type_mismatch);
  CHECK_FALSE(i.as_array().has_value());
  CHECK(i.as_array().error() == error::type_mismatch);
  CHECK_FALSE(i.as_object().has_value());
  CHECK(i.as_object().error() == error::type_mismatch);

  auto const b{json::value{true}};
  CHECK(*b.as_bool() == true);
  CHECK_FALSE(b.as_int().has_value());
  CHECK(b.as_int().error() == error::type_mismatch);

  auto const f{json::value{2.5}};
  CHECK(*f.as_float() == 2.5);
  CHECK(*f.as_int() == 2);  // floating accepted, truncated toward zero

  auto const s{json::value{"hello"}};
  CHECK(*s.as_string() == "hello");
  CHECK_FALSE(s.as_int().has_value());
  CHECK_FALSE(s.as_float().has_value());

  auto const nul{json::value{}};
  CHECK_FALSE(nul.as_bool().has_value());
  CHECK_FALSE(nul.as_int().has_value());
  CHECK_FALSE(nul.as_float().has_value());
  CHECK_FALSE(nul.as_string().has_value());
}

TEST_CASE("nexenne::serialization::json value - as_array / as_object references") {
  auto const arr{json::value{json::array{1, 2, 3}}};
  auto const a{arr.as_array()};
  REQUIRE(a.has_value());
  CHECK(a->get().size() == 3);
  CHECK(*a->get()[1].as_int() == 2);

  auto const obj{json::value{json::object{{"a", 1}, {"b", 2}}}};
  auto const o{obj.as_object()};
  REQUIRE(o.has_value());
  CHECK(o->get().size() == 2);
  CHECK(*obj["a"].as_int() == 1);
}

TEST_CASE("nexenne::serialization::json value - get and get_or") {
  auto const v{json::value{42}};
  CHECK(v.get<int>() == 42);
  CHECK(v.get<std::int64_t>() == 42);
  CHECK(v.get<double>() == 42.0);
  CHECK_FALSE(v.get<bool>().has_value());
  CHECK_FALSE(v.get<std::string>().has_value());

  CHECK(v.get_or<int>(0) == 42);
  CHECK(v.get_or<std::string>("x") == "x");  // mismatch -> fallback

  auto const s{json::value{"hi"}};
  CHECK(s.get<std::string>() == "hi");
  CHECK(s.get<std::string_view>() == "hi");
  CHECK_FALSE(s.get<int>().has_value());

  auto const b{json::value{true}};
  CHECK(b.get<bool>() == true);
  CHECK_FALSE(b.get<int>().has_value());  // bool is not int via get
}

TEST_CASE("nexenne::serialization::json value - operator[] builder syntax") {
  auto root{json::value{}};
  root["name"] = "alice";
  root["age"] = 30;
  root["admin"] = true;
  root["nested"]["deep"] = 99;
  CHECK(root.is_object());
  CHECK(*root["name"].as_string() == "alice");
  CHECK(*root["age"].as_int() == 30);
  CHECK(*root["admin"].as_bool() == true);
  CHECK(*root["nested"]["deep"].as_int() == 99);

  // const operator[]: missing key -> null sentinel.
  auto const& cref{root};
  CHECK(cref["missing"].is_null());
  CHECK(cref["name"].is_string());

  // const operator[] on a non-object -> null sentinel.
  auto const not_obj{json::value{42}};
  CHECK(not_obj["whatever"].is_null());
}

TEST_CASE("nexenne::serialization::json value - array element access") {
  auto arr{json::value{json::array{10, 20, 30}}};
  CHECK(*arr[0].as_int() == 10);
  CHECK(*arr[2].as_int() == 30);
  arr[1] = 99;
  CHECK(*arr[1].as_int() == 99);

  // const out-of-range index -> null sentinel.
  auto const& cref{arr};
  CHECK(cref[2].is_integer());
  CHECK(cref[99].is_null());

  // const index on non-array -> null sentinel.
  auto const not_arr{json::value{"s"}};
  CHECK(not_arr[0].is_null());
}

TEST_CASE("nexenne::serialization::json value - array_mut and object_mut") {
  auto arr{json::value{json::array{}}};
  arr.array_mut().push_back(json::value{1});
  arr.array_mut().push_back(json::value{2});
  CHECK(arr.as_array()->get().size() == 2);

  auto obj{json::value{json::object{}}};
  obj.object_mut().try_emplace("k", json::value{5});
  CHECK(*obj["k"].as_int() == 5);
}

TEST_CASE("nexenne::serialization::json value - equality") {
  CHECK(json::value{42} == json::value{42});
  CHECK_FALSE(json::value{42} == json::value{43});
  // integer and numerically-equal float are distinct kinds.
  CHECK_FALSE(json::value{2} == json::value{2.0});
  CHECK(json::value{json::array{1, 2}} == json::value{json::array{1, 2}});
  CHECK_FALSE(json::value{json::array{1, 2}} == json::value{json::array{1, 3}});
  CHECK(json::value{json::object{{"a", 1}}} == json::value{json::object{{"a", 1}}});
  CHECK(json::value{} == json::value{nullptr});
}

TEST_CASE("nexenne::serialization::json value - deep nesting copy") {
  auto v{json::value{json::object{
    {"a", json::array{json::object{{"b", json::array{1, 2, json::value{"deep"}}}}}},
  }}};
  auto const copy{v};
  CHECK(copy == v);
  CHECK(*copy.at_path("/a/0/b/2")->get().as_string() == "deep");
}

TEST_CASE("nexenne::serialization::json value - iterative destructor on 10000-deep nest") {
  // A recursive variant destructor would overflow the stack at this depth.
  // The iterative teardown must release it cleanly.
  auto v{json::value{json::array{}}};
  auto* cur{&v};
  for (auto i{0}; i < 10000; ++i) {
    cur->array_mut().push_back(json::value{json::array{}});
    cur = &cur->array_mut()[0];
  }
  REQUIRE(v.is_array());
  // Destructs here at depth 10000 without overflowing.
  CHECK(true);
}

TEST_CASE("nexenne::serialization::json value - iterative destructor on deep object nest") {
  auto v{json::value{json::object{}}};
  auto* cur{&v};
  for (auto i{0}; i < 10000; ++i) {
    cur->object_mut().try_emplace("k", json::value{json::object{}});
    cur = &cur->object_mut().find("k")->second;
  }
  REQUIRE(v.is_object());
  CHECK(true);
}

TEST_CASE("nexenne::serialization::json value - at_path RFC 6901") {
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

  // empty path returns the root unchanged.
  CHECK(root.at_path("")->get().is_object());

  // non-slash-prefixed path is invalid input.
  auto const bad{root.at_path("nope")};
  CHECK_FALSE(bad.has_value());
  CHECK(bad.error() == error::invalid_input);

  // missing segments report path_not_found.
  CHECK(root.at_path("/missing").error() == error::path_not_found);
  CHECK(root.at_path("/users/99/name").error() == error::path_not_found);
  CHECK(root.at_path("/users/x").error() == error::path_not_found);  // non-numeric array idx
}

TEST_CASE("nexenne::serialization::json value - at_path malformed array indices") {
  auto const root{json::parse("[10,20,30]")};
  REQUIRE(root.has_value());
  CHECK(*root->at_path("/0")->get().as_int() == 10);
  CHECK_FALSE(root->at_path("/01").has_value());                    // leading zero
  CHECK_FALSE(root->at_path("/18446744073709551618").has_value());  // overflows size_t
  CHECK_FALSE(root->at_path("/99").has_value());                    // out of range
  // "/" is the single reference token "" (RFC 6901); an array has no "" index.
  CHECK(root->at_path("/").error() == error::path_not_found);
}

TEST_CASE("nexenne::serialization::json parse - primitives") {
  CHECK(json::parse("null")->is_null());
  CHECK(*json::parse("true")->as_bool() == true);
  CHECK(*json::parse("false")->as_bool() == false);
  CHECK(*json::parse("42")->as_int() == 42);
  CHECK(*json::parse("-17")->as_int() == -17);
  CHECK(*json::parse("0")->as_int() == 0);
  CHECK(*json::parse("-0")->as_int() == 0);
  CHECK(*json::parse("3.14")->as_float() == 3.14);
  CHECK(*json::parse("1e3")->as_float() == 1000.0);
  CHECK(*json::parse("\"hi\"")->as_string() == "hi");
  CHECK(json::parse("\"\"")->as_string() == "");  // empty string
}

TEST_CASE("nexenne::serialization::json parse - whitespace handling") {
  CHECK(*json::parse("  42  ")->as_int() == 42);
  CHECK(*json::parse("\t\n\r 42\n")->as_int() == 42);
  CHECK(json::parse(" [ 1 , 2 , 3 ] ")->is_array());
  CHECK(json::parse("{\n  \"a\" : 1\n}")->is_object());
  // leading/trailing whitespace only, no value.
  CHECK_FALSE(json::parse("   ").has_value());
  CHECK_FALSE(json::parse("").has_value());
}

TEST_CASE("nexenne::serialization::json parse - empty array and object") {
  CHECK(json::parse("[]")->is_array());
  CHECK(json::parse("[]")->as_array()->get().empty());
  CHECK(json::parse("{}")->is_object());
  CHECK(json::parse("{}")->as_object()->get().empty());
  CHECK(json::parse("[ ]")->is_array());
  CHECK(json::parse("{ }")->is_object());
}

TEST_CASE("nexenne::serialization::json parse - array and object content") {
  auto const arr{json::parse("[1, 2, 3]")};
  REQUIRE(arr.has_value());
  REQUIRE(arr->is_array());
  CHECK(*(*arr)[0].as_int() == 1);
  CHECK(*(*arr)[2].as_int() == 3);

  auto const obj{json::parse("{\"a\":1,\"b\":\"x\"}")};
  REQUIRE(obj.has_value());
  REQUIRE(obj->is_object());
  CHECK(*(*obj)["a"].as_int() == 1);
  CHECK(*(*obj)["b"].as_string() == "x");

  // nested mix
  auto const nested{json::parse("{\"a\":1,\"b\":[1,2,3],\"c\":{\"d\":true}}")};
  REQUIRE(nested.has_value());
  CHECK(*nested->at_path("/c/d")->get().as_bool() == true);
  CHECK(*nested->at_path("/b/1")->get().as_int() == 2);
}

TEST_CASE("nexenne::serialization::json parse - round trip every type") {
  auto const docs{std::array<std::string_view, 9>{
    "null",
    "true",
    "false",
    "42",
    "-17",
    "3.14",
    "\"hello\"",
    "[1,2,3]",
    "{\"a\":1,\"b\":[1,2,3],\"c\":{\"d\":true}}",
  }};
  for (auto const d : docs) {
    auto const first{json::parse(d)};
    REQUIRE_MESSAGE(first.has_value(), d);
    auto const text{json::serialize(*first)};
    auto const second{json::parse(text)};
    REQUIRE_MESSAGE(second.has_value(), text);
    CHECK_MESSAGE(*first == *second, d);
  }
}

TEST_CASE("nexenne::serialization::json parse - number integer vs float discrimination") {
  CHECK(json::parse("42")->is_integer());
  CHECK(json::parse("-42")->is_integer());
  CHECK(json::parse("0")->is_integer());
  CHECK(json::parse("3.14")->is_floating());
  CHECK(json::parse("1e3")->is_floating());
  CHECK(json::parse("1E3")->is_floating());
  CHECK(json::parse("1.0")->is_floating());
  CHECK(json::parse("1e+3")->is_floating());
  CHECK(json::parse("1e-3")->is_floating());
  CHECK(json::parse("-2.5e10")->is_floating());
}

TEST_CASE("nexenne::serialization::json parse - number values") {
  CHECK(*json::parse("10")->as_int() == 10);
  CHECK(*json::parse("-9223372036854775808")->as_int() == std::numeric_limits<std::int64_t>::min());
  CHECK(*json::parse("9223372036854775807")->as_int() == std::numeric_limits<std::int64_t>::max());
  CHECK(*json::parse("1.5")->as_float() == 1.5);
  CHECK(*json::parse("0.0")->as_float() == 0.0);
  CHECK(*json::parse("1e3")->as_float() == 1000.0);
  CHECK(*json::parse("2.5e2")->as_float() == 250.0);
  CHECK(*json::parse("-2.5e-2")->as_float() == doctest::Approx(-0.025));
}

TEST_CASE("nexenne::serialization::json parse - RFC 8259 number strictness") {
  CHECK_FALSE(json::parse("01").has_value());  // leading zero
  CHECK_FALSE(json::parse("00").has_value());
  CHECK_FALSE(json::parse("-01").has_value());
  CHECK_FALSE(json::parse("1.").has_value());  // no digit after dot
  CHECK_FALSE(json::parse(".5").has_value());  // no integer part
  CHECK_FALSE(json::parse("1e").has_value());  // no digit in exponent
  CHECK_FALSE(json::parse("1e+").has_value());
  CHECK_FALSE(json::parse("1e-").has_value());
  CHECK_FALSE(json::parse("-").has_value());     // bare minus
  CHECK_FALSE(json::parse("+1").has_value());    // leading plus not allowed
  CHECK_FALSE(json::parse("0x10").has_value());  // hex not allowed
  CHECK_FALSE(json::parse("Infinity").has_value());
  CHECK_FALSE(json::parse("NaN").has_value());
  CHECK_FALSE(json::parse("1.2.3").has_value());

  // error code is invalid_number for malformed digits.
  CHECK(json::parse("1.").error().code == error::invalid_number);
  CHECK(json::parse("1e").error().code == error::invalid_number);
}

TEST_CASE("nexenne::serialization::json parse - over-large integer widens to double") {
  auto const r{json::parse("99999999999999999999")};  // beyond int64 range
  REQUIRE(r.has_value());
  CHECK(r->is_floating());
  CHECK(*r->as_float() == doctest::Approx(1e20));

  auto const neg{json::parse("-99999999999999999999")};
  REQUIRE(neg.has_value());
  CHECK(neg->is_floating());
}

TEST_CASE("nexenne::serialization::json parse - very large exponent and as_int rejection") {
  auto const big{json::parse("1e300")};
  REQUIRE(big.has_value());
  REQUIRE(big->is_floating());
  // historically-fixed UB: casting an out-of-range double to int64 is UB; must reject.
  CHECK_FALSE(big->as_int().has_value());
  CHECK(big->as_int().error() == error::type_mismatch);

  // in-range float still truncates.
  CHECK(*json::value{3.9}.as_int() == 3);
  CHECK(*json::value{-3.9}.as_int() == -3);

  // boundary: exactly int64 max as a double rounds up to 2^63, must reject (>= hi).
  auto const at_max{json::value{static_cast<double>(std::numeric_limits<std::int64_t>::max())}};
  CHECK_FALSE(at_max.as_int().has_value());
  // exactly int64 min as a double is representable and accepted.
  auto const at_min{json::value{static_cast<double>(std::numeric_limits<std::int64_t>::min())}};
  CHECK(at_min.as_int().has_value());
  CHECK(*at_min.as_int() == std::numeric_limits<std::int64_t>::min());

  // non-finite doubles rejected by as_int.
  CHECK_FALSE(json::value{std::numeric_limits<double>::infinity()}.as_int().has_value());
  CHECK_FALSE(json::value{-std::numeric_limits<double>::infinity()}.as_int().has_value());
  CHECK_FALSE(json::value{std::numeric_limits<double>::quiet_NaN()}.as_int().has_value());
}

TEST_CASE("nexenne::serialization::json parse - simple escapes") {
  CHECK(*json::parse("\"a\\nb\"")->as_string() == "a\nb");
  CHECK(*json::parse("\"\\\"x\\\"\"")->as_string() == "\"x\"");
  CHECK(*json::parse("\"\\\\\"")->as_string() == "\\");
  CHECK(*json::parse("\"\\/\"")->as_string() == "/");
  CHECK(*json::parse("\"\\b\"")->as_string() == "\b");
  CHECK(*json::parse("\"\\f\"")->as_string() == "\f");
  CHECK(*json::parse("\"\\r\"")->as_string() == "\r");
  CHECK(*json::parse("\"\\t\"")->as_string() == "\t");
  CHECK(*json::parse("\"tab\\there\"")->as_string() == "tab\there");
}

TEST_CASE("nexenne::serialization::json parse - u-escapes and UTF-8") {
  CHECK(*json::parse("\"\\u0041\"")->as_string() == "A");
  // U+00E9 (e-acute) -> 2-byte UTF-8 C3 A9.
  CHECK(*json::parse("\"\\u00e9\"")->as_string() == "\xc3\xa9");
  // U+20AC (euro) -> 3-byte UTF-8 E2 82 AC.
  CHECK(*json::parse("\"\\u20ac\"")->as_string() == "\xe2\x82\xac");
  // NUL via  .
  CHECK(*json::parse("\"\\u0000\"")->as_string() == std::string(1, '\0'));
  // case-insensitive hex digits decode identically.
  CHECK(*json::parse("\"\\u00E9\"")->as_string() == *json::parse("\"\\u00e9\"")->as_string());
  // raw UTF-8 in the source passes through untouched.
  CHECK(*json::parse("\"\xc3\xa9\"")->as_string() == "\xc3\xa9");
}

TEST_CASE("nexenne::serialization::json parse - surrogate pairs") {
  // valid surrogate pair: U+1D11E (musical G clef) -> 4-byte UTF-8 F0 9D 84 9E.
  auto const escaped{json::parse("\"\\uD834\\uDD1E\"")};
  REQUIRE(escaped.has_value());
  CHECK(*escaped->as_string() == "\xF0\x9D\x84\x9E");

  // the same code point given as raw UTF-8 bytes parses to the same string.
  auto const raw{json::parse("\"\xF0\x9D\x84\x9E\"")};
  REQUIRE(raw.has_value());
  CHECK(*raw->as_string() == "\xF0\x9D\x84\x9E");

  // U+10000 lowest non-BMP, as an escaped pair.
  auto const low{json::parse("\"\\uD800\\uDC00\"")};
  REQUIRE(low.has_value());
  CHECK(*low->as_string() == "\xF0\x90\x80\x80");
}

TEST_CASE("nexenne::serialization::json parse - invalid surrogates rejected") {
  CHECK_FALSE(json::parse("\"\\uDC00\"").has_value());         // lone low surrogate
  CHECK_FALSE(json::parse("\"\\uD800\"").has_value());         // lone high surrogate (EOF after)
  CHECK_FALSE(json::parse("\"\\uD800A\"").has_value());        // high then non-escape char
  CHECK_FALSE(json::parse("\"\\uD800x\"").has_value());        // high then non-escape char
  CHECK_FALSE(json::parse("\"\\uD800\\u0041\"").has_value());  // high then non-low surrogate
  CHECK_FALSE(json::parse("\"\\uD800\\uD800\"").has_value());  // high then high
  CHECK(json::parse("\"\\uD800\\uDC00\"").has_value());        // valid pair (U+10000)
}

TEST_CASE("nexenne::serialization::json parse - invalid escapes rejected") {
  CHECK_FALSE(json::parse("\"\\x\"").has_value());      // unknown escape
  CHECK_FALSE(json::parse("\"\\u12\"").has_value());    // short \u (only 2 hex before quote)
  CHECK_FALSE(json::parse("\"\\u12zz\"").has_value());  // non-hex in \u
  CHECK(json::parse("\"\\u12\"").error().code == error::invalid_escape);
  CHECK(json::parse("\"\\x\"").error().code == error::invalid_escape);
}

TEST_CASE("nexenne::serialization::json parse - control characters in strings rejected") {
  // a raw newline (0x0A) inside a string is illegal; must be escaped.
  auto const nl{std::string{"\"a\nb\""}};
  CHECK_FALSE(json::parse(nl).has_value());
  CHECK(json::parse(nl).error().code == error::invalid_string);

  auto const tab{std::string{"\"a\tb\""}};
  CHECK_FALSE(json::parse(tab).has_value());

  auto const nul_in{std::string{"\"a\0b\"", 5}};
  CHECK_FALSE(json::parse(nul_in).has_value());
}

TEST_CASE("nexenne::serialization::json parse - structural errors") {
  CHECK_FALSE(json::parse("nope").has_value());
  CHECK_FALSE(json::parse("{").has_value());
  CHECK_FALSE(json::parse("[").has_value());
  CHECK_FALSE(json::parse("}").has_value());
  CHECK_FALSE(json::parse("]").has_value());
  CHECK_FALSE(json::parse("[1,2,]").has_value());      // trailing comma
  CHECK_FALSE(json::parse("{\"a\":1,}").has_value());  // trailing comma object
  CHECK_FALSE(json::parse("{\"a\":1,").has_value());   // unterminated
  CHECK_FALSE(json::parse("[,]").has_value());         // bare comma
  CHECK_FALSE(json::parse("[1,,2]").has_value());      // double comma
  CHECK_FALSE(json::parse("[1 2]").has_value());       // missing comma
  CHECK_FALSE(json::parse("{\"a\" 1}").has_value());   // missing colon
  CHECK_FALSE(json::parse("{a:1}").has_value());       // unquoted key
  CHECK_FALSE(json::parse("{1:2}").has_value());       // non-string key
  CHECK_FALSE(json::parse("[1]extra").has_value());    // trailing garbage
  CHECK_FALSE(json::parse("truefalse").has_value());   // trailing garbage
  CHECK_FALSE(json::parse("nul").has_value());         // truncated literal
  CHECK_FALSE(json::parse("tru").has_value());
  CHECK_FALSE(json::parse("fals").has_value());
}

TEST_CASE("nexenne::serialization::json parse - duplicate keys rejected") {
  auto const r{json::parse("{\"a\":1,\"a\":2}")};
  CHECK_FALSE(r.has_value());
  CHECK(r.error().code == error::duplicate_key);
}

TEST_CASE("nexenne::serialization::json parse - error position tracking") {
  auto const r{json::parse("[1,\n2,\nx]")};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().line == 3);
  CHECK(r.error().column == 1);
}

TEST_CASE("nexenne::serialization::json parse - options trailing commas and comments") {
  auto opts{json::parse_options{}};
  opts.allow_trailing_commas = true;
  opts.allow_comments = true;
  CHECK(json::parse("[1,2,]", opts).has_value());
  CHECK(json::parse("{\"a\":1,}", opts).has_value());
  CHECK(json::parse("[1 /* x */, 2]", opts).has_value());
  CHECK(json::parse("[1, // tail\n 2]", opts).has_value());
  CHECK(json::parse("// lead\n42", opts).has_value());

  // unterminated block comment is still rejected.
  CHECK_FALSE(json::parse("42 /* unterminated", opts).has_value());
  CHECK(json::parse("42 /* closed */", opts).has_value());
}

TEST_CASE("nexenne::serialization::json parse - depth limit returns error, no overflow") {
  // depth far beyond the default 128: must report an error, never crash.
  auto deep{std::string(200, '[')};
  deep += std::string(200, ']');
  auto const r{json::parse(deep)};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == error::depth_limit_exceeded);

  auto ok{std::string(50, '[')};
  ok += std::string(50, ']');
  CHECK(json::parse(ok).has_value());

  // custom shallow limit.
  auto shallow{json::parse_options{.max_depth = 10}};
  auto d2{std::string(100, '[')};
  d2 += std::string(100, ']');
  CHECK_FALSE(json::parse(d2, shallow).has_value());

  // object nesting depth limit too.
  auto obj_deep{std::string{}};
  for (auto i{0}; i < 200; ++i)
    obj_deep += "{\"a\":";
  obj_deep += "1";
  for (auto i{0}; i < 200; ++i)
    obj_deep += "}";
  CHECK(json::parse(obj_deep).error().code == error::depth_limit_exceeded);
}

TEST_CASE("nexenne::serialization::json parse - truncated input never crashes") {
  auto const fragments{std::array<std::string_view, 24>{
    "\"unterminated",  // cut mid-string
    "\"escape\\",      // cut mid-escape
    "\"\\u",           // cut mid-\u, no digits
    "\"\\u12",         // cut mid-\u, partial digits
    "\"\\uD800",       // cut after high surrogate
    "\"\\uD800\\u",    // cut mid-low-surrogate-escape
    "123",             // (valid, control)
    "12.",             // cut mid-fraction
    "12e",             // cut mid-exponent
    "12e+",            // cut mid-exponent sign
    "-",               // bare minus
    "[1,2",            // unterminated array
    "[1,",             // unterminated array after comma
    "[",               // bare open bracket
    "{",               // bare open brace
    "{\"k\"",          // object key, no colon
    "{\"k\":",         // object, no value
    "{\"k\":1",        // object, no close
    "{\"k\":1,",       // object, dangling comma
    "tr",              // truncated true
    "nul",             // truncated null
    "fal",             // truncated false
    "\xff\xfe",        // garbage bytes
    "\x01\x02\x03",    // control bytes
  }};
  for (auto const f : fragments) {
    auto const r{json::parse(f)};
    // every malformed fragment must fail cleanly (no crash, no OOB).
    if (f == "123") {
      CHECK(r.has_value());  // the one valid control case
    } else {
      CHECK_MESSAGE(!r.has_value(), f);
    }
  }
}

TEST_CASE("nexenne::serialization::json parse - all prefixes of a valid document") {
  // Feeding every truncation of a valid document must never crash or read OOB.
  auto const full{std::string{"{\"name\":\"alice\",\"scores\":[42,73.5,null],\"ok\":true}"}};
  for (std::size_t n{0}; n <= full.size(); ++n) {
    auto const prefix{std::string_view{full}.substr(0, n)};
    auto const r{json::parse(prefix)};
    // Only the complete document should parse; every strict prefix is malformed.
    if (n == full.size()) {
      CHECK(r.has_value());
    } else {
      CHECK_MESSAGE(!r.has_value(), prefix);
    }
  }
}

TEST_CASE("nexenne::serialization::json parse - garbage and control bytes") {
  for (int b{1}; b < 0x20; ++b) {
    auto const s{std::string(1, static_cast<char>(b))};
    CHECK_FALSE(json::parse(s).has_value());  // control byte as document
  }
  CHECK_FALSE(json::parse("\x7f").has_value());
  CHECK_FALSE(json::parse("@#$%").has_value());
  CHECK_FALSE(json::parse(",").has_value());
  CHECK_FALSE(json::parse(":").has_value());
}

TEST_CASE("nexenne::serialization::json serialize - exact output per type") {
  CHECK(json::serialize(json::value{}) == "null");
  CHECK(json::serialize(json::value{true}) == "true");
  CHECK(json::serialize(json::value{false}) == "false");
  CHECK(json::serialize(json::value{42}) == "42");
  CHECK(json::serialize(json::value{-17}) == "-17");
  CHECK(json::serialize(json::value{0}) == "0");
  CHECK(json::serialize(json::value{"hi"}) == "\"hi\"");
  CHECK(json::serialize(json::value{json::array{}}) == "[]");
  CHECK(json::serialize(json::value{json::object{}}) == "{}");
  CHECK(json::serialize(json::value{json::array{1, 2, 3}}) == "[1,2,3]");
  CHECK(json::serialize(json::value{json::object{{"a", 1}}}) == "{\"a\":1}");
}

TEST_CASE("nexenne::serialization::json serialize - keys emitted in sorted order") {
  auto const v{json::value{json::object{{"z", 1}, {"a", 2}, {"m", 3}}}};
  CHECK(json::serialize(v) == "{\"a\":2,\"m\":3,\"z\":1}");
}

TEST_CASE("nexenne::serialization::json serialize - round trip") {
  auto const src{"{\"a\":1,\"b\":[1,2,3],\"c\":{\"d\":true}}"};
  auto const parsed{json::parse(src)};
  REQUIRE(parsed.has_value());
  CHECK(json::serialize(*parsed) == src);
}

TEST_CASE("nexenne::serialization::json serialize - string escapes") {
  CHECK(json::serialize(json::value{"a\nb\"c"}) == "\"a\\nb\\\"c\"");
  CHECK(json::serialize(json::value{"\b\f\r\t"}) == "\"\\b\\f\\r\\t\"");
  CHECK(json::serialize(json::value{"\\"}) == "\"\\\\\"");
  // control char below 0x20 with no shorthand -> \u00XX.
  CHECK(json::serialize(json::value{std::string(1, '\x01')}) == "\"\\u0001\"");
  CHECK(json::serialize(json::value{std::string(1, '\x1f')}) == "\"\\u001f\"");
}

TEST_CASE("nexenne::serialization::json serialize - ascii_only escapes non-ASCII") {
  auto opts{json::serialize_options{}};
  opts.ascii_only = true;
  // U+00E9 (C3 A9) -> é
  CHECK(json::serialize(json::value{"\xc3\xa9"}, opts) == "\"\\u00e9\"");
  // 4-byte UTF-8 (U+1D11E) -> surrogate pair 𝄞
  CHECK(json::serialize(json::value{"\xF0\x9D\x84\x9E"}, opts) == "\"\\ud834\\udd1e\"");
  // valid ASCII passes through untouched.
  CHECK(json::serialize(json::value{"abc"}, opts) == "\"abc\"");
  // malformed UTF-8 becomes U+FFFD replacement -> �
  CHECK(json::serialize(json::value{std::string("\xff", 1)}, opts) == "\"\\ufffd\"");
}

TEST_CASE("nexenne::serialization::json serialize - number formatting round-trips") {
  auto const nums{
    std::array<double, 7>{3.14, -2.5, 1e3, 1.23456789012345e-10, 0.0, -0.0, 1.7976931348623157e308}
  };
  for (auto const n : nums) {
    auto const text{json::serialize(json::value{n})};
    auto const back{json::parse(text)};
    REQUIRE_MESSAGE(back.has_value(), text);
    CHECK(*back->as_float() == doctest::Approx(n));
  }

  // integer-valued double keeps a ".0" so it reparses as a float.
  auto const two{json::parse(json::serialize(json::value{2.0}))};
  REQUIRE(two.has_value());
  CHECK(two->is_floating());

  // a large double rendered in integer form gets ".0" so it does not overflow on reparse.
  auto const big{json::parse(json::serialize(json::value{3.148e19}))};
  REQUIRE(big.has_value());
  CHECK(big->is_floating());
  CHECK(*big->as_float() == 3.148e19);

  // NaN/Inf serialize to null.
  CHECK(json::serialize(json::value{std::numeric_limits<double>::quiet_NaN()}) == "null");
  CHECK(json::serialize(json::value{std::numeric_limits<double>::infinity()}) == "null");
}

TEST_CASE("nexenne::serialization::json serialize - integer full range round-trips") {
  auto const ints{std::array<std::int64_t, 5>{
    0,
    std::numeric_limits<std::int64_t>::min(),
    std::numeric_limits<std::int64_t>::max(),
    -1,
    1234567890123456789LL,
  }};
  for (auto const i : ints) {
    auto const back{json::parse(json::serialize(json::value{i}))};
    REQUIRE(back.has_value());
    CHECK(*back->as_int() == i);
  }
}

TEST_CASE("nexenne::serialization::json serialize_pretty") {
  auto const v{json::value{json::object{
    {"a", 1},
    {"b", json::array{1, 2}},
  }}};
  auto const pretty{json::serialize_pretty(v)};
  CHECK(pretty.find('\n') != std::string::npos);
  CHECK(pretty.find("  ") != std::string::npos);
  // pretty and compact carry the same content (reparse equal).
  CHECK(*json::parse(pretty) == *json::parse(json::serialize(v)));

  // empty containers stay inline even when pretty.
  CHECK(json::serialize_pretty(json::value{json::array{}}) == "[]");
  CHECK(json::serialize_pretty(json::value{json::object{}}) == "{}");

  // custom indent width.
  auto opts{json::serialize_options{}};
  opts.indent = 4;
  auto const wide{json::serialize_pretty(json::value{json::object{{"a", 1}}}, opts)};
  CHECK(wide.find("    \"a\"") != std::string::npos);
}

TEST_CASE("nexenne::serialization::json writer - flat object") {
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
  CHECK(w.view() == "{\"name\":\"alice\",\"age\":30,\"admin\":true}");
  CHECK(w.bytes_written() == w.view().size());
  CHECK(w.depth() == 0);
}

TEST_CASE("nexenne::serialization::json writer - nested and all scalar types") {
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
  CHECK(w.view() == "{\"xs\":[1,2,3],\"nested\":{\"k\":null}}");
}

TEST_CASE("nexenne::serialization::json writer - empty containers") {
  auto buf{std::array<char, 64>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.begin_array().has_value());
  REQUIRE(w.end_array().has_value());
  CHECK(w.view() == "[]");

  w.reset();
  CHECK(w.bytes_written() == 0);
  REQUIRE(w.begin_object().has_value());
  REQUIRE(w.end_object().has_value());
  CHECK(w.view() == "{}");
}

TEST_CASE("nexenne::serialization::json writer - top-level scalar") {
  auto buf{std::array<char, 64>{}};
  {
    auto w{json::writer{buf}};
    REQUIRE(w.value(42).has_value());
    CHECK(w.is_complete());
    CHECK(w.view() == "42");
  }
  {
    auto w{json::writer{buf}};
    REQUIRE(w.value(true).has_value());
    CHECK(w.view() == "true");
  }
  {
    auto w{json::writer{buf}};
    REQUIRE(w.value_null().has_value());
    CHECK(w.view() == "null");
  }
}

TEST_CASE("nexenne::serialization::json writer - escapes") {
  auto buf{std::array<char, 64>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.value("a\nb\"c").has_value());
  CHECK(w.view() == "\"a\\nb\\\"c\"");
}

TEST_CASE("nexenne::serialization::json writer - control char escapes") {
  auto buf{std::array<char, 64>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.value(std::string_view{"\x01\x1f"}).has_value());
  CHECK(w.view() == "\"\\u0001\\u001f\"");
}

TEST_CASE("nexenne::serialization::json writer - float dot-zero suffix and NaN/Inf as null") {
  {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.value(2.0).has_value());
    // must reparse as a float, not an int.
    auto const back{json::parse(w.view())};
    REQUIRE(back.has_value());
    CHECK(back->is_floating());
  }
  {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.value(std::numeric_limits<double>::quiet_NaN()).has_value());
    CHECK(w.view() == "null");
  }
  {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.value(std::numeric_limits<double>::infinity()).has_value());
    CHECK(w.view() == "null");
  }
}

TEST_CASE("nexenne::serialization::json writer - buffer_full reported, no overflow") {
  SUBCASE("key does not fit") {
    auto buf{std::array<char, 4>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.begin_object().has_value());
    auto const r{w.key("long_key")};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  SUBCASE("value does not fit") {
    auto buf{std::array<char, 2>{}};
    auto w{json::writer{buf}};
    auto const r{w.value("toolong")};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  SUBCASE("integer does not fit") {
    auto buf{std::array<char, 1>{}};
    auto w{json::writer{buf}};
    auto const r{w.value(123456)};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
  SUBCASE("zero-size buffer rejects first brace") {
    auto buf{std::array<char, 0>{}};
    auto w{json::writer{buf}};
    auto const r{w.begin_object()};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::buffer_full);
  }
}

TEST_CASE("nexenne::serialization::json writer - structural errors") {
  SUBCASE("key in array context") {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.begin_array().has_value());
    auto const r{w.key("nope")};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::invalid_input);
  }
  SUBCASE("end_object with array open") {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.begin_array().has_value());
    auto const r{w.end_object()};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::invalid_input);
  }
  SUBCASE("end_array with object open") {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.begin_object().has_value());
    auto const r{w.end_array()};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::invalid_input);
  }
  SUBCASE("value where key expected") {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.begin_object().has_value());
    auto const r{w.value(1)};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::invalid_input);
  }
  SUBCASE("end_object before any value pending") {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    auto const r{w.end_object()};  // no object open
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::invalid_input);
  }
  SUBCASE("value after top-level done") {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.value(1).has_value());
    auto const r{w.value(2)};  // nothing more accepted at top level
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::invalid_input);
  }
  SUBCASE("end_object with dangling value expected") {
    auto buf{std::array<char, 64>{}};
    auto w{json::writer{buf}};
    REQUIRE(w.begin_object().has_value());
    REQUIRE(w.key("k").has_value());
    auto const r{w.end_object()};  // value expected, not close
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::invalid_input);
  }
}

TEST_CASE("nexenne::serialization::json writer - depth limit") {
  auto buf{std::array<char, 64>{}};
  auto w{json::writer<2>{buf}};
  REQUIRE(w.begin_array().has_value());
  REQUIRE(w.begin_array().has_value());
  auto const r{w.begin_array()};  // third level exceeds MaxDepth==2
  CHECK_FALSE(r.has_value());
  CHECK(r.error() == error::depth_limit_exceeded);
}

TEST_CASE("nexenne::serialization::json writer - reset reuses buffer") {
  auto buf{std::array<char, 64>{}};
  auto w{json::writer{buf}};
  REQUIRE(w.value(1).has_value());
  CHECK(w.bytes_written() == 1);
  w.reset();
  CHECK(w.bytes_written() == 0);
  CHECK(w.depth() == 0);
  REQUIRE(w.value(2).has_value());
  CHECK(w.view() == "2");
}

TEST_CASE("nexenne::serialization::json writer vs DOM serialize agree") {
  // Build the same document two ways; their compact text must match.
  auto const dom{json::value{json::object{
    {"admin", true},
    {"age", 30},
    {"name", "alice"},
  }}};
  auto const serialized{json::serialize(dom)};

  auto buf{std::array<char, 256>{}};
  auto w{json::writer{buf}};
  // DOM keys are sorted (admin, age, name); emit in the same order.
  REQUIRE(w.begin_object().has_value());
  REQUIRE(w.key("admin").has_value());
  REQUIRE(w.value(true).has_value());
  REQUIRE(w.key("age").has_value());
  REQUIRE(w.value(30).has_value());
  REQUIRE(w.key("name").has_value());
  REQUIRE(w.value("alice").has_value());
  REQUIRE(w.end_object().has_value());

  CHECK(w.view() == serialized);
}

struct recording_visitor : json::noop_visitor {
  int nulls{0}, bools{0}, ints{0}, floats{0}, strings{0}, keys{0};
  int begin_objs{0}, end_objs{0}, begin_arrs{0}, end_arrs{0};
  bool last_bool{false};
  std::int64_t last_int{0};
  double last_float{0.0};
  std::string last_string{};
  std::string last_key{};
  std::vector<std::string> events{};

  auto on_null() noexcept -> bool {
    ++nulls;
    events.emplace_back("null");
    return true;
  }

  auto on_bool(bool const b) noexcept -> bool {
    ++bools;
    last_bool = b;
    events.emplace_back(b ? "bool:true" : "bool:false");
    return true;
  }

  auto on_int(std::int64_t const i) noexcept -> bool {
    ++ints;
    last_int = i;
    events.emplace_back("int");
    return true;
  }

  auto on_float(double const d) noexcept -> bool {
    ++floats;
    last_float = d;
    events.emplace_back("float");
    return true;
  }

  auto on_string(std::string_view const s) noexcept -> bool {
    ++strings;
    last_string = s;
    events.emplace_back("str:" + std::string{s});
    return true;
  }

  auto on_key(std::string_view const s) noexcept -> bool {
    ++keys;
    last_key = s;
    events.emplace_back("key:" + std::string{s});
    return true;
  }

  auto on_begin_object() noexcept -> bool {
    ++begin_objs;
    events.emplace_back("{");
    return true;
  }

  auto on_end_object() noexcept -> bool {
    ++end_objs;
    events.emplace_back("}");
    return true;
  }

  auto on_begin_array() noexcept -> bool {
    ++begin_arrs;
    events.emplace_back("[");
    return true;
  }

  auto on_end_array() noexcept -> bool {
    ++end_arrs;
    events.emplace_back("]");
    return true;
  }
};

TEST_CASE("nexenne::serialization::json scan - event sequence for a known document") {
  auto v{recording_visitor{}};
  REQUIRE(json::scan("{\"a\":1,\"b\":[true,null,2.5,\"x\"]}", v).has_value());
  auto const expected{std::vector<std::string>{
    "{", "key:a", "int", "key:b", "[", "bool:true", "null", "float", "str:x", "]", "}"
  }};
  CHECK(v.events == expected);
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

TEST_CASE("nexenne::serialization::json scan - callback per value type") {
  auto v{recording_visitor{}};
  REQUIRE(json::scan("[null, true, false, -9, 2.5, \"hi\"]", v).has_value());
  CHECK(v.nulls == 1);
  CHECK(v.bools == 2);
  CHECK(v.last_bool == false);
  CHECK(v.ints == 1);
  CHECK(v.last_int == -9);
  CHECK(v.floats == 1);
  CHECK(v.last_float == doctest::Approx(2.5));
  CHECK(v.strings == 1);
  CHECK(v.last_string == "hi");
}

TEST_CASE("nexenne::serialization::json scan - strings passed raw and undecoded") {
  auto v{recording_visitor{}};
  REQUIRE(json::scan("[\"a\\nb\\\"\"]", v).has_value());
  CHECK(v.strings == 1);
  CHECK(v.last_string == "a\\nb\\\"");  // raw bytes, escape not decoded
  CHECK(v.last_string.find('\n') == std::string::npos);
}

TEST_CASE("nexenne::serialization::json scan - keys in document order") {
  auto v{recording_visitor{}};
  REQUIRE(json::scan("{\"first\":1,\"second\":2}", v).has_value());
  CHECK(v.keys == 2);
  CHECK(v.last_key == "second");
  CHECK(v.ints == 2);
}

TEST_CASE("nexenne::serialization::json scan - over-large integer goes to on_float") {
  auto v{recording_visitor{}};
  REQUIRE(json::scan("99999999999999999999", v).has_value());
  CHECK(v.floats == 1);
  CHECK(v.ints == 0);
}

TEST_CASE("nexenne::serialization::json scan - visitor abort short-circuits") {
  struct abort_on_key : json::noop_visitor {
    std::string captured;

    auto on_key(std::string_view k) noexcept -> bool {
      captured = k;
      return false;
    }
  };

  auto v{abort_on_key{}};
  auto const r{json::scan("{\"target\":42,\"unused\":99}", v)};
  CHECK_FALSE(r.has_value());
  CHECK(r.error() == error::invalid_input);
  CHECK(v.captured == "target");
}

TEST_CASE("nexenne::serialization::json scan - depth limit") {
  auto v{recording_visitor{}};
  SUBCASE("beyond cap rejected") {
    auto const r{json::scan<3>("[[[[1]]]]", v)};
    CHECK_FALSE(r.has_value());
    CHECK(r.error() == error::depth_limit_exceeded);
  }
  SUBCASE("at cap accepted") {
    auto const r{json::scan<8>("[[[[1]]]]", v)};
    REQUIRE(r.has_value());
    CHECK(v.begin_arrs == 4);
    CHECK(v.end_arrs == 4);
  }
}

TEST_CASE("nexenne::serialization::json scan - malformed input errors cleanly") {
  auto const fragments{std::array<std::string_view, 14>{
    "{\"a\":}",        // missing value
    "[1,2",            // unterminated array
    "{",               // bare brace
    "[",               // bare bracket
    "\"unterminated",  // cut mid-string
    "\"bad\\",         // trailing backslash, unterminated escape
    "01",              // leading zero
    "1.",              // mid-fraction
    "[1 2]",           // missing comma
    "{\"k\" 1}",       // missing colon
    "nul",             // truncated literal
    ",",               // bare comma
    "\xff",            // garbage byte
    "[1,2]extra",      // trailing garbage
  }};
  for (auto const f : fragments) {
    auto v{recording_visitor{}};
    auto const r{json::scan(f, v)};
    CHECK_MESSAGE(!r.has_value(), f);
  }
}

TEST_CASE("nexenne::serialization::json scan - all prefixes of a valid document") {
  auto const full{std::string{"{\"name\":\"alice\",\"xs\":[1,2,3],\"ok\":true}"}};
  for (std::size_t n{0}; n < full.size(); ++n) {
    auto v{recording_visitor{}};
    auto const r{json::scan(std::string_view{full}.substr(0, n), v)};
    CHECK_MESSAGE(!r.has_value(), std::string_view{full}.substr(0, n));
  }
  auto v{recording_visitor{}};
  CHECK(json::scan(std::string_view{full}, v).has_value());
}

TEST_CASE("nexenne::serialization::json scan - zero allocation contract") {
  static_assert(
    sizeof(json::detail::sax_engine<32>) <= 128,
    "SAX engine grew unexpectedly - review MCU footprint"
  );
  CHECK(true);
}

TEST_CASE("nexenne::serialization::json writer + scan round trip") {
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

  auto v{recording_visitor{}};
  REQUIRE(json::scan(w.view(), v).has_value());
  CHECK(v.keys == 3);
  CHECK(v.ints == 1);
  CHECK(v.floats == 1);
  CHECK(v.bools == 1);

  // and the writer's output parses through the DOM identically.
  auto const dom{json::parse(w.view())};
  REQUIRE(dom.has_value());
  CHECK(*dom->at_path("/id")->get().as_int() == 42);
  CHECK(*dom->at_path("/temp")->get().as_float() == 23.5);
  CHECK(*dom->at_path("/on")->get().as_bool() == true);
}

TEST_CASE("nexenne::serialization::json at_path resolves empty JSON-Pointer tokens") {
  // RFC 6901: "" is the whole document, "/" is the single token "" (the value at
  // the empty-string key), and "/a/" descends into a's "" child.
  auto const dom{json::parse(R"({"": 1, "a": {"": 2}, "x": 9})")};
  REQUIRE(dom.has_value());

  CHECK(dom->at_path("")->get().is_object());  // whole document

  auto const root_empty{dom->at_path("/")};  // token "" -> value at key ""
  REQUIRE(root_empty.has_value());
  REQUIRE(root_empty->get().is_integer());  // not the whole document
  CHECK(*root_empty->get().as_int() == 1);

  auto const a_empty{dom->at_path("/a/")};  // "a" then ""
  REQUIRE(a_empty.has_value());
  REQUIRE(a_empty->get().is_integer());  // not the object at "a"
  CHECK(*a_empty->get().as_int() == 2);

  CHECK(dom->at_path("/a")->get().is_object());
  CHECK(*dom->at_path("/x")->get().as_int() == 9);

  // "//" is two empty tokens, not one.
  auto const nested{json::parse(R"({"": {"": 5}})")};
  REQUIRE(nested.has_value());
  auto const both{nested->at_path("//")};
  REQUIRE(both.has_value());
  REQUIRE(both->get().is_integer());
  CHECK(*both->get().as_int() == 5);
  CHECK(nested->at_path("/")->get().is_object());  // one token -> the inner object
}

TEST_CASE("nexenne::serialization::json value equality is stack-safe on deep DOMs") {
  // Build a hand-made DOM far deeper than the parser's depth limit; a recursive
  // operator== would overflow the stack comparing it (the destructor is already
  // iterative, so building and destroying it is safe).
  auto build{[](int const depth, int const leaf) {
    auto v{json::value{static_cast<std::int64_t>(leaf)}};
    for (int i{0}; i < depth; ++i) {
      auto arr{json::array{}};
      arr.push_back(std::move(v));
      v = json::value{std::move(arr)};
    }
    return v;
  }};
  auto const a{build(100000, 7)};
  auto const b{build(100000, 7)};
  auto const c{build(100000, 8)};  // identical except the innermost leaf
  CHECK(a == b);
  CHECK_FALSE(a == c);
}

}  // namespace
