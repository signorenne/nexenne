/**
 * @file
 * @brief Round-trip the same data through every serialization codec.
 *
 * Shows the four flavours side by side: a JSON DOM (parse, query by JSON
 * Pointer, re-serialize), the schema-driven binary writer/reader, the CBOR
 * codec, and COBS framing that turns a payload containing 0x00 into a zero-free
 * frame. Every operation returns std::expected; the buffers are sized so the
 * calls succeed.
 *
 * The last three tours go deeper: a CBOR nested array-of-maps decoded by
 * peeking the type of each item, building a JSON DOM in code and reading it
 * back typed, and the two error paths (a buffer too small to write into, a
 * truncated buffer to read from) showing that failures arrive as values.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/serialization/serialization.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace ser = nexenne::serialization;

}  // namespace

auto main() -> int {
  // 1. JSON: parse, query by JSON Pointer, re-serialize.
  if (auto doc{ser::json::parse(R"({"name":"alice","scores":[10,20,30]})")}) {
    auto const name{doc->at_path("/name")->get().as_string()};
    auto const second{doc->at_path("/scores/1")->get().as_int()};
    std::println("json   : name={} scores[1]={}", *name, *second);
    std::println("json   : re-serialized {}", ser::json::serialize(*doc));
  }

  // 2. Schema-driven binary: no tags, both sides walk the same order.
  {
    auto buf{std::array<std::byte, 64>{}};
    auto w{ser::binary::writer{buf}};
    nexenne::utility::discard(w.write(std::uint32_t{42}));
    nexenne::utility::discard(w.write(std::string_view{"hi"}));
    auto r{ser::binary::reader{w.written()}};
    auto const n{r.read<std::uint32_t>()};
    auto const s{r.read_string()};
    std::println("binary : {} bytes -> n={} s={}", w.written().size(), *n, *s);
  }

  // 3. CBOR: self-describing, peek the type then read.
  {
    auto buf{std::array<std::byte, 64>{}};
    auto w{ser::cbor::writer{buf}};
    nexenne::utility::discard(w.write_uint(1000));
    nexenne::utility::discard(w.write_string("cbor"));
    auto r{ser::cbor::reader{w.written()}};
    auto const a{r.read_uint()};
    auto const b{r.read_string()};
    std::println("cbor   : {} bytes -> {} {}", w.written().size(), *a, *b);
  }

  // 4. COBS: frame a payload that itself contains 0x00 so a 0x00 can delimit it.
  {
    std::array<std::byte, 4> const payload{
      std::byte{0x11}, std::byte{0x00}, std::byte{0x22}, std::byte{0x00}
    };
    auto frame{std::array<std::byte, ser::cobs::cobs_max_encoded_size(4)>{}};
    auto const enc{ser::cobs::encode(payload, frame)};
    auto zero_free{true};
    for (auto i{std::size_t{0}}; i < *enc; ++i) {
      if (frame[i] == std::byte{0}) {
        zero_free = false;
      }
    }
    auto out{std::array<std::byte, 4>{}};
    auto const dec{ser::cobs::decode(std::span<std::byte const>{frame.data(), *enc}, out)};
    std::println(
      "cobs   : {}-byte payload -> {}-byte frame (zero-free={}) -> decoded {} bytes",
      payload.size(),
      *enc,
      zero_free,
      *dec
    );
  }

  // 5. CBOR nested: an array of two maps, each {"id": uint, "ok": bool}. CBOR is
  // self-describing, so the reader does not need the schema: it reads the array
  // length, then for each element reads the map pair-count and peeks the type of
  // each value before choosing the matching read_* call.
  {
    auto buf{std::array<std::byte, 64>{}};
    auto w{ser::cbor::writer{buf}};
    nexenne::utility::discard(w.write_array_header(2));
    for (auto const& [id, ok] : std::array<std::pair<int, bool>, 2>{{{1, true}, {2, false}}}) {
      nexenne::utility::discard(w.write_map_header(2));
      nexenne::utility::discard(w.write_string("id"));
      nexenne::utility::discard(w.write_uint(static_cast<std::uint64_t>(id)));
      nexenne::utility::discard(w.write_string("ok"));
      nexenne::utility::discard(w.write_bool(ok));
    }

    auto r{ser::cbor::reader{w.written()}};
    auto const rows{r.read_array_header()};
    auto decoded{std::string{}};
    for (auto i{std::uint64_t{0}}; i < *rows; ++i) {
      auto const pairs{r.read_map_header()};
      auto id{std::uint64_t{0}};
      auto ok{false};
      for (auto p{std::uint64_t{0}}; p < *pairs; ++p) {
        auto const key{r.read_string()};
        // Peek the value's type to branch, rather than assuming a fixed layout.
        if (*r.peek_type() == ser::cbor::type::boolean) {
          ok = *r.read_bool();
        } else {
          id = *r.read_uint();
        }
        nexenne::utility::discard(key);
      }
      decoded += std::format("{}{}:{}", i == 0 ? "" : " ", id, ok);
    }
    std::println("cbor   : {} rows decoded by peeking types -> {}", *rows, decoded);
  }

  // 6. JSON DOM built in code (not parsed), then read back typed. operator[]
  // mutates, get<T>() returns std::optional for a safe typed read, and the
  // object serialises in sorted-key order for a deterministic string.
  {
    auto doc{ser::json::value{ser::json::object{
      {"name", "bob"},
      {"level", std::int64_t{4}},
      {"tags", ser::json::array{"new", "vip"}},
    }}};
    doc["level"] = std::int64_t{5};  // mutate in place through the DOM
    auto const level{doc["level"].get<std::int64_t>()};
    std::println(
      "json   : built DOM, level={} -> {}", level.value_or(-1), ser::json::serialize(doc)
    );
  }

  // 7. Error paths arrive as values, never exceptions. A writer with no room
  // reports buffer_full; a reader past the end reports buffer_underrun; a bad
  // JSON document reports a parse_error whose .code names the failure and whose
  // line/column point at it.
  {
    auto tiny{std::array<std::byte, 1>{}};
    auto w{ser::binary::writer{tiny}};
    auto const full{w.write(std::uint32_t{0})};  // needs 4 bytes, has 1
    auto under{ser::binary::reader{std::span<std::byte const>{tiny.data(), 0}}};
    auto const empty{under.read<std::uint32_t>()};
    auto const bad{ser::json::parse(R"({"unterminated":)")};
    std::println(
      "errors : write={} read={} parse={} (at col {})",
      ser::to_string(full.error()),
      ser::to_string(empty.error()),
      ser::to_string(bad.error().code),
      bad.error().column
    );
  }

  return 0;
}
