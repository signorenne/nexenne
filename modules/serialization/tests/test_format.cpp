#include <doctest/doctest.h>

#include <array>
#include <format>
#include <string>
#include <string_view>

#include <nexenne/serialization/format.hpp>

namespace {

using namespace nexenne::serialization;

TEST_CASE("nexenne::serialization::error - std::format matches to_string") {
  constexpr std::array errors{
    error::invalid_input,
    error::unexpected_character,
    error::unexpected_end,
    error::invalid_number,
    error::invalid_string,
    error::invalid_escape,
    error::duplicate_key,
    error::depth_limit_exceeded,
    error::type_mismatch,
    error::path_not_found,
    error::buffer_full,
    error::buffer_underrun,
    error::string_too_long,
  };
  for (auto const e : errors) {
    CHECK(std::format("{}", e) == std::string{to_string(e)});
  }
}

TEST_CASE("nexenne::serialization::error - format spec applies to the name") {
  // The inherited string formatter honours width and alignment.
  CHECK(std::format("{:>16}", error::buffer_full) == "     buffer_full");
  CHECK(std::format("[{:<8}]", error::type_mismatch) == "[type_mismatch]");
}

TEST_CASE("nexenne::serialization::json::value - std::format matches serialize") {
  auto const v{json::value{json::object{
    {"name", "alice"},
    {"age", 30},
    {"admin", true},
    {"scores", json::array{42, 73, 99}},
  }}};
  CHECK(std::format("{}", v) == json::serialize(v));
}

TEST_CASE("nexenne::serialization::json::value - scalars format like serialize") {
  CHECK(std::format("{}", json::value{}) == "null");
  CHECK(std::format("{}", json::value{true}) == "true");
  CHECK(std::format("{}", json::value{42}) == "42");
  CHECK(std::format("{}", json::value{"hi"}) == "\"hi\"");
}

TEST_CASE("nexenne::serialization::cbor::type - std::format matches to_string") {
  constexpr std::array kinds{
    cbor::type::unsigned_int, cbor::type::negative_int, cbor::type::byte_string,
    cbor::type::text_string,  cbor::type::array_header, cbor::type::map_header,
    cbor::type::boolean,      cbor::type::null,         cbor::type::undefined,
    cbor::type::floating,
  };
  for (auto const k : kinds) {
    CHECK(std::format("{}", k) == std::string{cbor::to_string(k)});
  }
  CHECK(std::format("{}", cbor::type::text_string) == "text_string");
  CHECK(std::format("{:>12}", cbor::type::null) == "        null");
}

TEST_CASE("nexenne::serialization::msgpack::type - std::format matches to_string") {
  constexpr std::array kinds{
    msgpack::type::nil,          msgpack::type::boolean, msgpack::type::integer,
    msgpack::type::floating,     msgpack::type::string,  msgpack::type::binary,
    msgpack::type::array_header, msgpack::type::map_header,
  };
  for (auto const k : kinds) {
    CHECK(std::format("{}", k) == std::string{msgpack::to_string(k)});
  }
  CHECK(std::format("{}", msgpack::type::array_header) == "array_header");
}

TEST_CASE("nexenne::serialization::json::value::kind - std::format matches to_string") {
  constexpr std::array kinds{
    json::value::kind::null_kind,    json::value::kind::boolean_kind,
    json::value::kind::integer_kind, json::value::kind::floating_kind,
    json::value::kind::string_kind,  json::value::kind::array_kind,
    json::value::kind::object_kind,
  };
  for (auto const k : kinds) {
    CHECK(std::format("{}", k) == std::string{json::to_string(k)});
  }
  CHECK(std::format("{}", json::value::kind::object_kind) == "object");
}

TEST_CASE("nexenne::serialization::json::parse_error - std::format renders a diagnostic") {
  auto const e{json::parse_error{.code = error::invalid_string, .offset = 41, .line = 3, .column = 12}
  };
  CHECK(std::format("{}", e) == "invalid_string at line 3, column 12 (offset 41)");
  CHECK(std::format("{}", e) == json::to_string(e));
}

TEST_CASE("nexenne::serialization::header - std::format renders magic and version") {
  auto const h{header{.magic = 0x4E455801u, .version = 2}};
  CHECK(std::format("{}", h) == "{magic: 0x4e455801, version: 2}");
  CHECK(std::format("{}", h) == to_string(h));
}

}  // namespace
