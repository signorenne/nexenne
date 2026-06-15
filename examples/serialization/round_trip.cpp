/**
 * @file
 * @brief Round-trip the same data through every serialization codec.
 *
 * Shows the four flavours side by side: a JSON DOM (parse, query by JSON
 * Pointer, re-serialize), the schema-driven binary writer/reader, the CBOR
 * codec, and COBS framing that turns a payload containing 0x00 into a zero-free
 * frame. Every operation returns std::expected; the buffers are sized so the
 * calls succeed.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <string_view>

#include <nexenne/serialization/serialization.hpp>

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
    static_cast<void>(w.write(std::uint32_t{42}));
    static_cast<void>(w.write(std::string_view{"hi"}));
    auto r{ser::binary::reader{w.written()}};
    auto const n{r.read<std::uint32_t>()};
    auto const s{r.read_string()};
    std::println("binary : {} bytes -> n={} s={}", w.written().size(), *n, *s);
  }

  // 3. CBOR: self-describing, peek the type then read.
  {
    auto buf{std::array<std::byte, 64>{}};
    auto w{ser::cbor::writer{buf}};
    static_cast<void>(w.write_uint(1000));
    static_cast<void>(w.write_string("cbor"));
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

  return 0;
}
