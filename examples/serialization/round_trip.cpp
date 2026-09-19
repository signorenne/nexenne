/**
 * @file
 * @brief Round-trip the same data through every serialization codec.
 *
 * Shows the four flavours side by side, then goes deeper. Every operation
 * returns std::expected; the buffers are sized so the calls succeed:
 *
 *   1. JSON: parse, query by JSON Pointer, re-serialize.
 *   2. Schema-driven binary: no tags, both sides walk the same order. Fields
 *      are spelled with the fixed-width integer aliases: write and read take
 *      only a binary::fixed_width_scalar, so the bytes cannot change with the
 *      target.
 *   3. CBOR: self-describing, so peek the type, then read.
 *   4. COBS: frame a payload that itself contains 0x00, so a 0x00 can delimit
 *      the frame.
 *   5. CBOR nested: an array of two maps, each {"id": uint, "ok": bool}. The
 *      reader does not need the schema: it reads the array length, then for
 *      each element reads the map pair count and peeks each value's type
 *      before choosing the matching read call.
 *   6. A JSON DOM built in code (not parsed), then read back typed. operator[]
 *      mutates in place, get<T>() returns std::optional for a safe typed read,
 *      and the object serializes in sorted-key order for a deterministic
 *      string.
 *   7. Error paths arrive as values, never exceptions: a writer with no room
 *      (a 4-byte write into 1 byte) reports buffer_full, a reader past the end
 *      reports buffer_underrun, and a bad JSON document reports a parse_error
 *      whose code names the failure and whose line and column point at it.
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
#include <nexenne/utility/ignore.hpp>

namespace {

namespace ser = nexenne::serialization;

}  // namespace

auto main() -> int {
  if (auto doc{ser::json::parse(R"({"name":"alice","scores":[10,20,30]})")}) {
    auto const name{doc->at_path("/name")->get().as_string()};
    auto const second{doc->at_path("/scores/1")->get().as_int()};
    std::println("json   : name={} scores[1]={}", *name, *second);
    std::println("json   : re-serialized {}", ser::json::serialize(*doc));
  }

  {
    auto buf{std::array<std::byte, 64>{}};
    auto w{ser::binary::writer{buf}};
    nexenne::utility::ignore(w.write(std::uint32_t{42}));
    nexenne::utility::ignore(w.write(std::string_view{"hi"}));
    auto r{ser::binary::reader{w.written()}};
    auto const n{r.read<std::uint32_t>()};
    auto const s{r.read_string()};
    std::println("binary : {} bytes -> n={} s={}", w.written().size(), *n, *s);
  }

  {
    auto buf{std::array<std::byte, 64>{}};
    auto w{ser::cbor::writer{buf}};
    nexenne::utility::ignore(w.write_uint(1000));
    nexenne::utility::ignore(w.write_string("cbor"));
    auto r{ser::cbor::reader{w.written()}};
    auto const a{r.read_uint()};
    auto const b{r.read_string()};
    std::println("cbor   : {} bytes -> {} {}", w.written().size(), *a, *b);
  }

  {
    std::array<std::byte, 4> const payload{
      std::byte{0x11}, std::byte{0x00}, std::byte{0x22}, std::byte{0x00}
    };
    auto frame{std::array<std::byte, ser::cobs::max_encoded_size(4)>{}};
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

  {
    auto buf{std::array<std::byte, 64>{}};
    auto w{ser::cbor::writer{buf}};
    nexenne::utility::ignore(w.write_array_header(2));
    for (auto const& [id, ok] : std::array<std::pair<int, bool>, 2>{{{1, true}, {2, false}}}) {
      nexenne::utility::ignore(w.write_map_header(2));
      nexenne::utility::ignore(w.write_string("id"));
      nexenne::utility::ignore(w.write_uint(static_cast<std::uint64_t>(id)));
      nexenne::utility::ignore(w.write_string("ok"));
      nexenne::utility::ignore(w.write_bool(ok));
    }

    auto r{ser::cbor::reader{w.written()}};
    auto const rows{r.read_array_header()};
    auto decoded{std::string{}};
    for (auto i{std::uint64_t{0}}; i < *rows; ++i) {
      auto const pairs{r.read_map_header()};
      auto id{std::uint64_t{0}};
      auto ok{false};
      for (auto p{std::uint64_t{0}}; p < *pairs; ++p) {
        [[maybe_unused]] auto const key{r.read_string()};
        if (*r.peek_type() == ser::cbor::type::boolean) {
          ok = *r.read_bool();
        } else {
          id = *r.read_uint();
        }
      }
      decoded += std::format("{}{}:{}", i == 0 ? "" : " ", id, ok);
    }
    std::println("cbor   : {} rows decoded by peeking types -> {}", *rows, decoded);
  }

  {
    auto doc{ser::json::value{ser::json::object{
      {"name", "bob"},
      {"level", std::int64_t{4}},
      {"tags", ser::json::array{"new", "vip"}},
    }}};
    doc["level"] = std::int64_t{5};
    auto const level{doc["level"].get<std::int64_t>()};
    std::println(
      "json   : built DOM, level={} -> {}", level.value_or(-1), ser::json::serialize(doc)
    );
  }

  {
    auto tiny{std::array<std::byte, 1>{}};
    auto w{ser::binary::writer{tiny}};
    auto const full{w.write(std::uint32_t{0})};
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
