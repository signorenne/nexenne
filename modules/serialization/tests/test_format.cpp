#include <doctest/doctest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <sstream>
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
    cbor::type::unsigned_int,
    cbor::type::negative_int,
    cbor::type::byte_string,
    cbor::type::text_string,
    cbor::type::array_header,
    cbor::type::map_header,
    cbor::type::boolean,
    cbor::type::null,
    cbor::type::undefined,
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
    msgpack::type::nil,
    msgpack::type::boolean,
    msgpack::type::integer,
    msgpack::type::floating,
    msgpack::type::string,
    msgpack::type::binary,
    msgpack::type::array_header,
    msgpack::type::map_header,
  };
  for (auto const k : kinds) {
    CHECK(std::format("{}", k) == std::string{msgpack::to_string(k)});
  }
  CHECK(std::format("{}", msgpack::type::array_header) == "array_header");
}

TEST_CASE("nexenne::serialization::json::value::kind - std::format matches to_string") {
  constexpr std::array kinds{
    json::value::kind::null_kind,
    json::value::kind::boolean_kind,
    json::value::kind::integer_kind,
    json::value::kind::floating_kind,
    json::value::kind::string_kind,
    json::value::kind::array_kind,
    json::value::kind::object_kind,
  };
  for (auto const k : kinds) {
    CHECK(std::format("{}", k) == std::string{json::to_string(k)});
  }
  CHECK(std::format("{}", json::value::kind::object_kind) == "object");
}

TEST_CASE("nexenne::serialization::json::parse_error - std::format renders a diagnostic") {
  auto const e{
    json::parse_error{.code = error::invalid_string, .offset = 41, .line = 3, .column = 12}
  };
  CHECK(std::format("{}", e) == "invalid_string at line 3, column 12 (offset 41)");
  CHECK(std::format("{}", e) == json::to_string(e));
}

TEST_CASE("nexenne::serialization::header - std::format renders magic and version") {
  auto const h{header{.magic = 0x4E455801u, .version = 2}};
  CHECK(std::format("{}", h) == "{magic: 0x4e455801, version: 2}");
  CHECK(std::format("{}", h) == to_string(h));
}

/**
 * @brief Whether \c to_string and \c operator<< give exactly the formatter's text.
 *
 * @tparam T Type printable through all three layers.
 * @param value Value to print.
 *
 * @return \c true when all three renderings are identical.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
auto three_layers_agree(T const& value) -> bool {
  auto os{std::ostringstream{}};
  os << value;
  auto const formatted{std::format("{}", value)};
  return std::string{to_string(value)} == formatted && os.str() == formatted;
}

TEST_CASE("nexenne::serialization - to_string and operator<< print the formatter's text") {
  using json::to_string;
  CHECK(three_layers_agree(error::invalid_escape));
  CHECK(three_layers_agree(header{.magic = 0x4E455801u, .version = 2}));
  CHECK(three_layers_agree(cbor::type::text_string));
  CHECK(three_layers_agree(msgpack::type::map_header));
  CHECK(three_layers_agree(json::value::kind::array_kind));
  CHECK(three_layers_agree(
    json::parse_error{.code = error::invalid_string, .offset = 41, .line = 3, .column = 12}
  ));
  auto const doc{json::parse(R"({"n":[1,true]})")};
  REQUIRE(doc.has_value());
  CHECK(three_layers_agree(*doc));
  CHECK(json::to_string(*doc) == R"({"n":[1,true]})");
}

TEST_CASE("nexenne::serialization::json options print every knob") {
  CHECK(
    std::format("{}", json::parse_options{})
    == "json::parse_options(allow_comments=false, allow_trailing_commas=false, max_depth=128)"
  );
  auto const relaxed{json::parse_options{.allow_comments = true, .max_depth = 8}};
  CHECK(
    std::format("{}", relaxed)
    == "json::parse_options(allow_comments=true, allow_trailing_commas=false, max_depth=8)"
  );
  CHECK(three_layers_agree(relaxed));

  auto const pretty{json::serialize_options{.indent = 4, .ascii_only = true}};
  CHECK(std::format("{}", pretty) == "json::serialize_options(indent=4, ascii_only=true)");
  CHECK(three_layers_agree(pretty));
}

TEST_CASE("nexenne::serialization::binary reader and writer print their progress") {
  auto buf{std::array<std::byte, 16>{}};
  auto w{binary::writer{buf}};
  REQUIRE(w.write(std::uint32_t{7}));
  CHECK(std::format("{}", w) == "binary::writer(bytes_written=4, bytes_remaining=12)");
  CHECK(three_layers_agree(w));

  auto r{binary::reader{w.written()}};
  r.max_string_size() = 64;
  REQUIRE(r.read<std::uint16_t>());
  CHECK(
    std::format("{}", r) == "binary::reader(bytes_read=2, bytes_remaining=2, max_string_size=64)"
  );
  CHECK(three_layers_agree(r));
}

TEST_CASE("nexenne::serialization::cbor and msgpack readers and writers print their progress") {
  auto cbor_buf{std::array<std::byte, 16>{}};
  auto cw{cbor::writer{cbor_buf}};
  REQUIRE(cw.write_bool(true));
  CHECK(std::format("{}", cw) == "cbor::writer(bytes_written=1, bytes_remaining=15)");
  CHECK(three_layers_agree(cw));
  auto cr{cbor::reader{cw.written()}};
  CHECK(std::format("{}", cr) == "cbor::reader(bytes_read=0, bytes_remaining=1)");
  REQUIRE(cr.read_bool());
  CHECK(std::format("{}", cr) == "cbor::reader(bytes_read=1, bytes_remaining=0)");
  CHECK(three_layers_agree(cr));

  auto msgpack_buf{std::array<std::byte, 8>{}};
  auto mw{msgpack::writer{msgpack_buf}};
  REQUIRE(mw.write_bool(false));
  CHECK(std::format("{}", mw) == "msgpack::writer(bytes_written=1, bytes_remaining=7)");
  CHECK(three_layers_agree(mw));
  auto mr{msgpack::reader{mw.written()}};
  REQUIRE(mr.read_bool());
  CHECK(std::format("{}", mr) == "msgpack::reader(bytes_read=1, bytes_remaining=0)");
  CHECK(three_layers_agree(mr));
}

TEST_CASE("nexenne::serialization::json::writer prints its progress and depth") {
  auto buf{std::array<char, 64>{}};
  auto w{json::writer<8>{buf}};
  REQUIRE(w.begin_array());
  CHECK(
    std::format("{}", w)
    == "json::writer(bytes_written=1, bytes_remaining=63, depth=1, max_depth=8, complete=false)"
  );
  REQUIRE(w.end_array());
  CHECK(
    std::format("{}", w)
    == "json::writer(bytes_written=2, bytes_remaining=62, depth=0, max_depth=8, complete=true)"
  );
  CHECK(three_layers_agree(w));
}

template <typename T>
concept json_to_string = requires(T const& t) { json::to_string(t); };

static_assert(json_to_string<json::value>);
static_assert(!json_to_string<int>);
static_assert(!json_to_string<char const*>);

}  // namespace
