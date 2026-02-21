/**
 * @file
 * @brief Tests for nexenne::algorithm encodings (hex, url, base64, base32, cobs).
 *
 * Every base codec is pinned to the RFC 4648 known-answer vectors and COBS to
 * the vectors from the Cheshire and Baker paper, then exercised for round-trip
 * fidelity over random inputs of every group-boundary length, for the error
 * paths (invalid byte, truncated token, exhausted buffer), and for the codec
 * niceties: whitespace skipping, case insensitivity, and omitted padding.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/algorithm/encoding/alphabet.hpp>
#include <nexenne/algorithm/encoding/base_n.hpp>
#include <nexenne/algorithm/encoding/cobs.hpp>
#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/algorithm/encoding/url.hpp>

namespace {

namespace alg = nexenne::algorithm;
using alg::codec_error;

[[nodiscard]] auto bytes_of(std::string_view const s) -> std::span<std::uint8_t const> {
  return {reinterpret_cast<std::uint8_t const*>(s.data()), s.size()};
}

[[nodiscard]] auto vbytes(std::string_view const s) -> std::vector<std::uint8_t> {
  return {s.begin(), s.end()};
}

struct lcg {
  std::uint64_t state{0x243F6A8885A308D3ull};

  auto byte() -> std::uint8_t {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<std::uint8_t>(state >> 24);
  }
};

[[nodiscard]] auto random_bytes(lcg& gen, std::size_t const n) -> std::vector<std::uint8_t> {
  auto v{std::vector<std::uint8_t>{}};
  v.reserve(n);
  for (auto i{std::size_t{0}}; i < n; ++i) {
    v.push_back(gen.byte());
  }
  return v;
}

// alphabet

TEST_CASE("nexenne::algorithm::codec_alphabet forward, reverse, and distinctness") {
  static constexpr alg::codec_alphabet<4> alpha{"WXYZ"};
  static_assert(alpha.is_distinct());
  static_assert(alpha.size == 4);
  static_assert(alpha.encode(0) == 'W');
  static_assert(alpha.encode(3) == 'Z');
  static_assert(alpha.decode('Y') == 2);
  static_assert(alpha.decode('?') == -1);  // not a member
  static_assert(alpha.decode('w') == -1);  // case sensitive
  static_assert(alg::codec_alphabet{"WXYZ"}.is_distinct());
  static_assert(!alg::codec_alphabet{"WXYW"}.is_distinct());  // duplicate detected
  CHECK(alpha.encode(1) == 'X');
  CHECK(alpha.decode('Z') == 3);
}

TEST_CASE("nexenne::algorithm::to_string names every codec_error") {
  CHECK(alg::to_string(codec_error::invalid_input) == "invalid_input");
  CHECK(alg::to_string(codec_error::buffer_too_small) == "buffer_too_small");
  CHECK(alg::to_string(codec_error::incomplete_input) == "incomplete_input");
}

// generic base_n engine (the shared core behind hex/base32/base64)

TEST_CASE("nexenne::algorithm::base_n is usable directly and at compile time") {
  // base64 of "f" computed entirely at compile time through the generic engine.
  static constexpr auto enc4{[] {
    auto out{std::array<char, 4>{}};
    auto const in{std::array<std::uint8_t, 1>{std::uint8_t{'f'}}};
    auto const n{alg::base_n_encode<alg::base64_std_spec>(
      std::span<std::uint8_t const>{in}, std::span<char>{out}
    )};
    return n.has_value() ? out : std::array<char, 4>{};
  }()};
  static_assert(enc4[0] == 'Z' && enc4[1] == 'g' && enc4[2] == '=' && enc4[3] == '=');

  // The generic decode matches the named wrapper on an RFC vector.
  auto buf{std::array<std::uint8_t, 8>{}};
  auto const r{
    alg::base_n_decode<alg::base32_std_spec>("MZXW6YTBOI======", std::span<std::uint8_t>{buf})
  };
  REQUIRE(r.has_value());
  CHECK(
    std::vector<std::uint8_t>(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(*r))
    == vbytes("foobar")
  );

  // The derived bit width and group sizes are correct for each member.
  static_assert(
    alg::base64_std_spec.bits == 6 && alg::base64_std_spec.group_in == 3
    && alg::base64_std_spec.group_out == 4
  );
  static_assert(
    alg::base32_std_spec.bits == 5 && alg::base32_std_spec.group_in == 5
    && alg::base32_std_spec.group_out == 8
  );
  static_assert(
    alg::base16_lower_spec.bits == 4 && alg::base16_lower_spec.group_in == 1
    && alg::base16_lower_spec.group_out == 2
  );
}

// hex

TEST_CASE("nexenne::algorithm::hex RFC 4648 base16 vectors") {
  CHECK(alg::hex_encode(bytes_of("")) == "");
  CHECK(alg::hex_encode(bytes_of("f")) == "66");
  CHECK(alg::hex_encode(bytes_of("fo")) == "666f");
  CHECK(alg::hex_encode(bytes_of("foobar")) == "666f6f626172");
  CHECK(alg::hex_encode(bytes_of("fo"), /*uppercase=*/true) == "666F");
}

TEST_CASE("nexenne::algorithm::hex decode is liberal: whitespace and mixed case") {
  CHECK(*alg::hex_decode("666F6f") == vbytes("foo"));
  CHECK(*alg::hex_decode("66 6f\t62\n61 72\r") == vbytes("fobar"));
  CHECK(alg::hex_decode("")->empty());
}

TEST_CASE("nexenne::algorithm::hex decode error paths") {
  CHECK(alg::hex_decode("12345").error() == codec_error::incomplete_input);  // odd nibble
  CHECK(alg::hex_decode("12zz").error() == codec_error::invalid_input);      // non-hex
  auto small{std::array<std::uint8_t, 1>{}};
  CHECK(
    alg::hex_decode("AABB", std::span<std::uint8_t>{small}).error() == codec_error::buffer_too_small
  );
}

TEST_CASE("nexenne::algorithm::hex buffered encode sizing") {
  CHECK(alg::hex_encoded_size(7) == 14);
  auto buf{std::array<char, 3>{}};
  CHECK(
    alg::hex_encode(bytes_of("AB"), std::span<char>{buf}).error() == codec_error::buffer_too_small
  );
}

// url

TEST_CASE("nexenne::algorithm::url strict percent-encoding vectors") {
  CHECK(alg::url_encode("hello world") == "hello%20world");
  CHECK(alg::url_encode("a+b=c&d/e") == "a%2Bb%3Dc%26d%2Fe");
  CHECK(alg::url_encode("AZaz09-._~") == "AZaz09-._~");  // unreserved, unchanged
  CHECK(*alg::url_decode("hello%20world") == "hello world");
  CHECK(*alg::url_decode("a%2Bb") == "a+b");
  CHECK(*alg::url_decode("a+b") == "a+b");  // strict: '+' is literal
}

TEST_CASE("nexenne::algorithm::url form-urlencoded uses plus for space") {
  CHECK(alg::form_url_encode("hello world") == "hello+world");
  CHECK(alg::form_url_encode("a b+c") == "a+b%2Bc");
  CHECK(*alg::form_url_decode("hello+world") == "hello world");
  CHECK(*alg::form_url_decode("a%2Bb") == "a+b");
}

TEST_CASE("nexenne::algorithm::url decode error paths") {
  CHECK(alg::url_decode("%4").error() == codec_error::incomplete_input);
  CHECK(alg::url_decode("abc%").error() == codec_error::incomplete_input);
  CHECK(alg::url_decode("%ZZ").error() == codec_error::invalid_input);
  CHECK(alg::url_decode("%2G").error() == codec_error::invalid_input);
}

TEST_CASE("nexenne::algorithm::url round-trips every byte value") {
  auto gen{lcg{}};
  for (auto len{std::size_t{0}}; len <= 120; ++len) {
    CAPTURE(len);
    auto const data{random_bytes(gen, len)};
    auto const s{std::string{reinterpret_cast<char const*>(data.data()), data.size()}};
    CHECK(*alg::url_decode(alg::url_encode(s)) == s);
    CHECK(*alg::form_url_decode(alg::form_url_encode(s)) == s);
  }
}

// base64

TEST_CASE("nexenne::algorithm::base64 RFC 4648 vectors") {
  CHECK(alg::base64_encode(bytes_of("")) == "");
  CHECK(alg::base64_encode(bytes_of("f")) == "Zg==");
  CHECK(alg::base64_encode(bytes_of("fo")) == "Zm8=");
  CHECK(alg::base64_encode(bytes_of("foo")) == "Zm9v");
  CHECK(alg::base64_encode(bytes_of("foob")) == "Zm9vYg==");
  CHECK(alg::base64_encode(bytes_of("fooba")) == "Zm9vYmE=");
  CHECK(alg::base64_encode(bytes_of("foobar")) == "Zm9vYmFy");
  CHECK(*alg::base64_decode("Zm9vYmFy") == vbytes("foobar"));
  CHECK(*alg::base64_decode("Zg==") == vbytes("f"));
}

TEST_CASE("nexenne::algorithm::base64url uses -_ and omits padding") {
  auto const tricky{std::array<std::uint8_t, 3>{0xFB, 0xFF, 0xBF}};
  CHECK(alg::base64_encode(std::span<std::uint8_t const>{tricky}) == "+/+/");
  CHECK(alg::base64url_encode(std::span<std::uint8_t const>{tricky}) == "-_-_");
  CHECK(alg::base64url_encode(bytes_of("f")) == "Zg");  // no padding
  CHECK(*alg::base64url_decode("-_-_") == std::vector<std::uint8_t>{0xFB, 0xFF, 0xBF});
  // The URL-safe decoder rejects the standard +/ alphabet.
  CHECK(alg::base64url_decode("+/+/").error() == codec_error::invalid_input);
}

TEST_CASE("nexenne::algorithm::base64 decode tolerates whitespace and missing padding") {
  CHECK(*alg::base64_decode("Zm9v\nYmFy") == vbytes("foobar"));
  CHECK(*alg::base64_decode("Zg") == vbytes("f"));    // padding omitted
  CHECK(*alg::base64_decode("Zm8") == vbytes("fo"));  // padding omitted
}

TEST_CASE("nexenne::algorithm::base64 decode error paths") {
  CHECK(alg::base64_decode("Zm9v!").error() == codec_error::invalid_input);   // non-alphabet
  CHECK(alg::base64_decode("Z").error() == codec_error::incomplete_input);    // lone char
  CHECK(alg::base64_decode("Zg==Zg").error() == codec_error::invalid_input);  // data after pad
  auto small{std::array<std::uint8_t, 2>{}};
  CHECK(
    alg::base64_decode("Zm9v", std::span<std::uint8_t>{small}).error()
    == codec_error::buffer_too_small
  );
}

TEST_CASE("nexenne::algorithm::base64 size helpers are exact") {
  CHECK(alg::base64_encoded_size(0) == 0);
  CHECK(alg::base64_encoded_size(1) == 4);
  CHECK(alg::base64_encoded_size(3) == 4);
  CHECK(alg::base64_encoded_size(6) == 8);
  CHECK(alg::base64url_encoded_size(1) == 2);
  CHECK(alg::base64url_encoded_size(2) == 3);
  CHECK(alg::base64url_encoded_size(3) == 4);
}

// base32

TEST_CASE("nexenne::algorithm::base32 RFC 4648 vectors") {
  CHECK(alg::base32_encode(bytes_of("")) == "");
  CHECK(alg::base32_encode(bytes_of("f")) == "MY======");
  CHECK(alg::base32_encode(bytes_of("fo")) == "MZXQ====");
  CHECK(alg::base32_encode(bytes_of("foo")) == "MZXW6===");
  CHECK(alg::base32_encode(bytes_of("foob")) == "MZXW6YQ=");
  CHECK(alg::base32_encode(bytes_of("fooba")) == "MZXW6YTB");
  CHECK(alg::base32_encode(bytes_of("foobar")) == "MZXW6YTBOI======");
  CHECK(*alg::base32_decode("MZXW6YTBOI======") == vbytes("foobar"));
}

TEST_CASE("nexenne::algorithm::base32hex RFC 4648 vectors") {
  CHECK(alg::base32hex_encode(bytes_of("f")) == "CO======");
  CHECK(alg::base32hex_encode(bytes_of("foo")) == "CPNMU===");
  CHECK(alg::base32hex_encode(bytes_of("foobar")) == "CPNMUOJ1E8======");
  CHECK(*alg::base32hex_decode("CPNMUOJ1E8======") == vbytes("foobar"));
}

TEST_CASE("nexenne::algorithm::base32 decode is case-insensitive and pad-tolerant") {
  CHECK(*alg::base32_decode("mzxw6ytboi") == vbytes("foobar"));   // lowercase, no padding
  CHECK(*alg::base32_decode("MZXW6YTB OI") == vbytes("foobar"));  // whitespace skipped
}

TEST_CASE("nexenne::algorithm::base32 decode error paths") {
  CHECK(alg::base32_decode("MZX").error() == codec_error::incomplete_input);  // 3-char tail invalid
  CHECK(alg::base32_decode("0189").error() == codec_error::invalid_input);  // 0/1/8/9 not in alpha
}

// cobs

TEST_CASE("nexenne::algorithm::cobs Cheshire-Baker reference vectors") {
  struct entry {
    std::vector<std::uint8_t> raw;
    std::vector<std::uint8_t> encoded;
  };

  auto const cases{std::array<entry, 5>{
    entry{{}, {0x01}},
    entry{{0x00}, {0x01, 0x01}},
    entry{{0x00, 0x00}, {0x01, 0x01, 0x01}},
    entry{{0x11, 0x22, 0x00, 0x33}, {0x03, 0x11, 0x22, 0x02, 0x33}},
    entry{{0x11, 0x22, 0x33, 0x44}, {0x05, 0x11, 0x22, 0x33, 0x44}},
  }};
  for (auto const& c : cases) {
    auto enc{std::vector<std::uint8_t>(alg::cobs_encoded_max_size(c.raw.size()))};
    auto const er{
      alg::cobs_encode(std::span<std::uint8_t const>{c.raw}, std::span<std::uint8_t>{enc})
    };
    REQUIRE(er.has_value());
    enc.resize(*er);
    CHECK(enc == c.encoded);

    auto dec{std::vector<std::uint8_t>(c.encoded.size())};
    auto const dr{
      alg::cobs_decode(std::span<std::uint8_t const>{c.encoded}, std::span<std::uint8_t>{dec})
    };
    REQUIRE(dr.has_value());
    dec.resize(*dr);
    CHECK(dec == c.raw);
  }
}

TEST_CASE("nexenne::algorithm::cobs round-trips, including 254-byte run boundaries") {
  auto gen{lcg{}};
  // Lengths around the 254-byte block boundary and well past it, plus inputs
  // that are all zero and all non-zero.
  for (auto const len :
       {std::size_t{0},
        std::size_t{1},
        std::size_t{253},
        std::size_t{254},
        std::size_t{255},
        std::size_t{300},
        std::size_t{509}}) {
    CAPTURE(len);
    for (auto const mode : {0, 1, 2}) {  // 0: random, 1: all zero, 2: all 0xFF
      auto raw{std::vector<std::uint8_t>(len)};
      for (auto i{std::size_t{0}}; i < len; ++i) {
        raw[i] = (mode == 0) ? gen.byte() : (mode == 1 ? std::uint8_t{0} : std::uint8_t{0xFF});
      }
      auto enc{std::vector<std::uint8_t>(alg::cobs_encoded_max_size(len))};
      auto const er{
        alg::cobs_encode(std::span<std::uint8_t const>{raw}, std::span<std::uint8_t>{enc})
      };
      REQUIRE(er.has_value());
      enc.resize(*er);
      // Encoded form is free of the 0x00 delimiter.
      for (auto const b : enc) {
        CHECK(b != 0u);
      }
      auto dec{std::vector<std::uint8_t>(enc.size())};
      auto const dr{
        alg::cobs_decode(std::span<std::uint8_t const>{enc}, std::span<std::uint8_t>{dec})
      };
      REQUIRE(dr.has_value());
      dec.resize(*dr);
      CHECK(dec == raw);
    }
  }
}

TEST_CASE("nexenne::algorithm::cobs decode error paths") {
  auto out{std::array<std::uint8_t, 16>{}};
  // A 0x00 code byte is never valid in a COBS stream.
  auto const stray{std::array<std::uint8_t, 2>{0x01, 0x00}};
  CHECK(
    alg::cobs_decode(std::span<std::uint8_t const>{stray}, std::span<std::uint8_t>{out}).error()
    == codec_error::invalid_input
  );
  // A code byte promising more data than remains is incomplete.
  auto const truncated{std::array<std::uint8_t, 2>{0x05, 0x11}};
  CHECK(
    alg::cobs_decode(std::span<std::uint8_t const>{truncated}, std::span<std::uint8_t>{out}).error()
    == codec_error::incomplete_input
  );
  auto tiny{std::array<std::uint8_t, 1>{}};
  auto const big{std::array<std::uint8_t, 3>{0x03, 0x11, 0x22}};
  CHECK(
    alg::cobs_decode(std::span<std::uint8_t const>{big}, std::span<std::uint8_t>{tiny}).error()
    == codec_error::buffer_too_small
  );
}

TEST_CASE("nexenne::algorithm::url round-trips every single byte value 0..255") {
  auto all{std::string{}};
  for (auto v{0}; v < 256; ++v) {
    all.push_back(static_cast<char>(v));
  }
  CHECK(*alg::url_decode(alg::url_encode(all)) == all);
  CHECK(*alg::form_url_decode(alg::form_url_encode(all)) == all);
}

TEST_CASE("nexenne::algorithm encode reports buffer_too_small") {
  auto tiny{std::array<char, 1>{}};
  CHECK(
    alg::base64_encode(bytes_of("foobar"), std::span<char>{tiny}).error()
    == codec_error::buffer_too_small
  );
  CHECK(
    alg::base32_encode(bytes_of("foobar"), std::span<char>{tiny}).error()
    == codec_error::buffer_too_small
  );
  CHECK(
    alg::base64url_encode(bytes_of("foobar"), std::span<char>{tiny}).error()
    == codec_error::buffer_too_small
  );
  auto small{std::array<std::uint8_t, 1>{}};
  auto const raw{std::array<std::uint8_t, 4>{1, 2, 3, 4}};
  CHECK(
    alg::cobs_encode(std::span<std::uint8_t const>{raw}, std::span<std::uint8_t>{small}).error()
    == codec_error::buffer_too_small
  );
}

// Generic round-trip across the byte-to-text codecs and every group boundary.

template <typename Enc, typename Dec>
void roundtrip_text_codec(Enc enc, Dec dec) {
  auto gen{lcg{}};
  for (auto len{std::size_t{0}}; len <= 130; ++len) {
    CAPTURE(len);
    auto const data{random_bytes(gen, len)};
    auto const encoded{enc(std::span<std::uint8_t const>{data})};
    auto const decoded{dec(std::string_view{encoded})};
    REQUIRE(decoded.has_value());
    CHECK(*decoded == data);
  }
}

TEST_CASE("nexenne::algorithm encode/decode round-trip over random inputs") {
  SUBCASE("hex") {
    roundtrip_text_codec(
      [](auto s) { return alg::hex_encode(s); }, [](auto s) { return alg::hex_decode(s); }
    );
  }
  SUBCASE("base64") {
    roundtrip_text_codec(
      [](auto s) { return alg::base64_encode(s); }, [](auto s) { return alg::base64_decode(s); }
    );
  }
  SUBCASE("base64url") {
    roundtrip_text_codec(
      [](auto s) { return alg::base64url_encode(s); },
      [](auto s) { return alg::base64url_decode(s); }
    );
  }
  SUBCASE("base32") {
    roundtrip_text_codec(
      [](auto s) { return alg::base32_encode(s); }, [](auto s) { return alg::base32_decode(s); }
    );
  }
  SUBCASE("base32hex") {
    roundtrip_text_codec(
      [](auto s) { return alg::base32hex_encode(s); },
      [](auto s) { return alg::base32hex_decode(s); }
    );
  }
}

// The heap-free overload writes exactly what the heap overload returns.

TEST_CASE("nexenne::algorithm buffered encode matches heap encode") {
  auto gen{lcg{}};
  for (auto len{std::size_t{0}}; len <= 80; ++len) {
    CAPTURE(len);
    auto const data{random_bytes(gen, len)};
    auto const span{std::span<std::uint8_t const>{data}};

    auto hbuf{std::string(alg::base64_encoded_size(len), '\0')};
    auto const hr{alg::base64_encode(span, std::span<char>{hbuf.data(), hbuf.size()})};
    REQUIRE(hr.has_value());
    hbuf.resize(*hr);
    CHECK(hbuf == alg::base64_encode(span));

    auto b32{std::string(alg::base32_encoded_size(len), '\0')};
    auto const br{alg::base32_encode(span, std::span<char>{b32.data(), b32.size()})};
    REQUIRE(br.has_value());
    b32.resize(*br);
    CHECK(b32 == alg::base32_encode(span));
  }
}

// Exhaustive round-trip over every 1-byte input (all codecs) and every 2-byte
// input (the codecs with non-trivial tail packing). Catches any bit-packing or
// tail bug a random sweep might miss.
template <typename Enc, typename Dec>
void exhaustive_roundtrip(Enc enc, Dec dec, bool const two_byte) {
  for (auto v{0}; v < 256; ++v) {
    auto const data{std::vector<std::uint8_t>{static_cast<std::uint8_t>(v)}};
    auto const d{dec(std::string_view{enc(std::span<std::uint8_t const>{data})})};
    REQUIRE(d.has_value());
    CHECK(*d == data);
  }
  if (two_byte) {
    for (auto a{0}; a < 256; ++a) {
      for (auto b{0}; b < 256; ++b) {
        auto const data{
          std::vector<std::uint8_t>{static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b)}
        };
        auto const d{dec(std::string_view{enc(std::span<std::uint8_t const>{data})})};
        REQUIRE(d.has_value());
        CHECK(*d == data);
      }
    }
  }
}

TEST_CASE("nexenne::algorithm exhaustive 1- and 2-byte round-trip") {
  SUBCASE("hex") {
    exhaustive_roundtrip(
      [](auto s) { return alg::hex_encode(s); }, [](auto s) { return alg::hex_decode(s); }, false
    );
  }
  SUBCASE("base64") {
    exhaustive_roundtrip(
      [](auto s) { return alg::base64_encode(s); },
      [](auto s) { return alg::base64_decode(s); },
      true
    );
  }
  SUBCASE("base64url") {
    exhaustive_roundtrip(
      [](auto s) { return alg::base64url_encode(s); },
      [](auto s) { return alg::base64url_decode(s); },
      false
    );
  }
  SUBCASE("base32") {
    exhaustive_roundtrip(
      [](auto s) { return alg::base32_encode(s); },
      [](auto s) { return alg::base32_decode(s); },
      true
    );
  }
  SUBCASE("base32hex") {
    exhaustive_roundtrip(
      [](auto s) { return alg::base32hex_encode(s); },
      [](auto s) { return alg::base32hex_decode(s); },
      false
    );
  }
}

TEST_CASE("nexenne::algorithm::base_n decode is constexpr-evaluable") {
  static constexpr auto decoded{[] {
    auto out{std::array<std::uint8_t, 3>{}};
    static_cast<void>(alg::base_n_decode<alg::base64_std_spec>("Zm9v", std::span<std::uint8_t>{out})
    );
    return out;
  }()};
  static_assert(decoded[0] == 'f' && decoded[1] == 'o' && decoded[2] == 'o');
  CHECK(decoded[2] == 'o');
}

TEST_CASE("nexenne::algorithm::url buffer limits and hex-escape case") {
  auto tiny{std::array<char, 2>{}};
  CHECK(
    alg::url_encode("hello world", std::span<char>{tiny}).error() == codec_error::buffer_too_small
  );
  auto one{std::array<char, 1>{}};
  CHECK(alg::url_decode("%41%42", std::span<char>{one}).error() == codec_error::buffer_too_small);
  // A high byte encodes to an uppercase escape; lowercase escapes still decode.
  CHECK(alg::url_encode(std::string_view{"\xff"}) == "%FF");
  CHECK(*alg::url_decode("%2b%2F") == "+/");
}

TEST_CASE("nexenne::algorithm::base64 padding tolerance and cross-alphabet rejection") {
  CHECK(*alg::base64_decode("Zg=") == vbytes("f"));    // single trailing pad
  CHECK(*alg::base64_decode("Zg===") == vbytes("f"));  // surplus padding ignored
  CHECK(alg::base64_decode("ab-_").error() == codec_error::invalid_input);     // url chars rejected
  CHECK(alg::base64url_decode("ab+/").error() == codec_error::invalid_input);  // std chars rejected
}

TEST_CASE("nexenne::algorithm::base32 rejects padding mid-stream; base32hex full vectors") {
  CHECK(alg::base32_decode("MY==MY").error() == codec_error::invalid_input);
  CHECK(alg::base32hex_encode(bytes_of("fo")) == "CPNG====");
  CHECK(alg::base32hex_encode(bytes_of("foob")) == "CPNMUOG=");
  CHECK(alg::base32hex_encode(bytes_of("fooba")) == "CPNMUOJ1");
}

TEST_CASE("nexenne::algorithm::cobs encodes a maximal 254-byte run") {
  // A run of 254 non-zero bytes fills a block: leading 0xFF, then the 254 bytes,
  // then a final 0x01 code byte (no implied trailing zero).
  auto const raw{std::vector<std::uint8_t>(254, std::uint8_t{0x07})};
  auto enc{std::vector<std::uint8_t>(alg::cobs_encoded_max_size(raw.size()))};
  auto const n{alg::cobs_encode(std::span<std::uint8_t const>{raw}, std::span<std::uint8_t>{enc})};
  REQUIRE(n.has_value());
  enc.resize(*n);
  REQUIRE(enc.size() == 256);
  CHECK(enc.front() == 0xFFu);
  CHECK(enc.back() == 0x01u);
  auto dec{std::vector<std::uint8_t>(enc.size())};
  auto const dn{alg::cobs_decode(std::span<std::uint8_t const>{enc}, std::span<std::uint8_t>{dec})};
  REQUIRE(dn.has_value());
  dec.resize(*dn);
  CHECK(dec == raw);
}

TEST_CASE("nexenne::algorithm encoding round-trips on a large random buffer") {
  auto gen{lcg{}};
  auto const data{random_bytes(gen, 4096)};
  auto const in{std::span<std::uint8_t const>{data}};
  {
    auto const dec{alg::base64_decode(alg::base64_encode(in))};
    REQUIRE(dec.has_value());
    CHECK(*dec == data);
  }
  {
    auto const dec{alg::base32_decode(alg::base32_encode(in))};
    REQUIRE(dec.has_value());
    CHECK(*dec == data);
  }
  {
    auto const dec{alg::hex_decode(alg::hex_encode(in))};
    REQUIRE(dec.has_value());
    CHECK(*dec == data);
  }
}

}  // namespace
