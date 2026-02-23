/**
 * @file
 * @brief Tests for nexenne::algorithm checksums (Adler-32 and the CRC engine).
 *
 * CRCs are pinned to the reveng catalogue check values for "123456789" across
 * every preset, then differentially validated against an independent bit-serial
 * reference over random inputs so the generated tables are exercised on more
 * than the one canonical string. The modular-sum family (Adler-32 and
 * Fletcher-16/32/64) is pinned to published vectors and checked against a naive
 * per-unit reference, including inputs spanning several blocks and partial final
 * units. Streaming contexts and seed chaining are verified across every split
 * point.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <nexenne/algorithm/checksum/crc.hpp>
#include <nexenne/algorithm/checksum/modular_sum.hpp>

namespace {

namespace alg = nexenne::algorithm;

[[nodiscard]] auto bytes_of(std::string_view const s) -> std::span<std::uint8_t const> {
  return {reinterpret_cast<std::uint8_t const*>(s.data()), s.size()};
}

// A deterministic byte source for building test inputs.
struct lcg {
  std::uint64_t state{0x243F6A8885A308D3ull};

  auto next() -> std::uint64_t {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state >> 11;
  }

  auto byte() -> std::uint8_t {
    return static_cast<std::uint8_t>(next());
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

// Independent references, written from the algorithm definitions rather than
// the implementation under test, so agreement is real cross-validation.

[[nodiscard]] auto
naive_adler32(std::span<std::uint8_t const> const bytes, std::uint32_t seed) -> std::uint32_t {
  auto a{(seed & 0xFFFFu) % 65521u};
  auto b{((seed >> 16u) & 0xFFFFu) % 65521u};
  for (auto const byte : bytes) {
    a = (a + byte) % 65521u;  // modulo every byte, the textbook formulation
    b = (b + a) % 65521u;
  }
  return (b << 16u) | a;
}

// Per-unit-modulo modular sum, the textbook formulation, parameterised like the
// engine: little-endian units, zero-padded final unit, modulo on every step.
[[nodiscard]] auto naive_modular_sum(
  std::span<std::uint8_t const> const bytes,
  std::size_t const unit,
  std::size_t const w,
  std::uint64_t const m,
  std::uint64_t const init1
) -> std::uint64_t {
  auto sum1{init1 % m};
  auto sum2{std::uint64_t{0}};
  for (auto i{std::size_t{0}}; i < bytes.size(); i += unit) {
    auto u{std::uint64_t{0}};
    for (auto j{std::size_t{0}}; j < unit; ++j) {
      if (i + j < bytes.size()) {
        u |= static_cast<std::uint64_t>(bytes[i + j]) << (8 * j);
      }
    }
    sum1 = (sum1 + u) % m;
    sum2 = (sum2 + sum1) % m;
  }
  return (sum2 << w) | sum1;
}

[[nodiscard]] auto reflect_n(std::uint64_t const value, std::size_t const n) -> std::uint64_t {
  auto out{std::uint64_t{0}};
  for (auto i{std::size_t{0}}; i < n; ++i) {
    if ((value >> i) & 1u) {
      out |= std::uint64_t{1} << (n - 1 - i);
    }
  }
  return out;
}

// Bit-serial CRC straight from the Rocksoft model: reflect each input byte when
// RefIn, process MSB-first against the polynomial, reflect the register when
// RefOut, XOR out. No lookup table, so it shares no code path with crc<Spec>.
template <alg::crc_spec Spec>
[[nodiscard]] auto crc_bitwise(std::span<std::uint8_t const> const data) ->
  typename decltype(Spec)::value_type {
  using value_type = typename decltype(Spec)::value_type;
  constexpr auto width{Spec.width};
  auto const mask{
    (width == sizeof(value_type) * 8) ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1
  };
  auto const top_bit{std::uint64_t{1} << (width - 1)};

  auto reg{static_cast<std::uint64_t>(Spec.init) & mask};
  for (auto const raw : data) {
    auto const b{Spec.ref_in ? reflect_n(raw, 8) : std::uint64_t{raw}};
    reg ^= b << (width - 8);
    for (auto bit{0}; bit < 8; ++bit) {
      reg = (reg & top_bit) ? ((reg << 1) ^ Spec.poly) : (reg << 1);
      reg &= mask;
    }
  }
  if (Spec.ref_out) {
    reg = reflect_n(reg, width);
  }
  return static_cast<value_type>((reg ^ Spec.xor_out) & mask);
}

// Adler-32: zlib known-answer vectors.

static_assert(alg::adler32(std::span<std::uint8_t const>{}) == 1u);

TEST_CASE("nexenne::algorithm::adler32 known-answer vectors") {
  CHECK(alg::adler32(std::string_view{""}) == 1u);
  CHECK(alg::adler32(std::string_view{"a"}) == 0x00620062u);
  CHECK(alg::adler32(std::string_view{"abc"}) == 0x024D0127u);
  CHECK(alg::adler32(std::string_view{"Wikipedia"}) == 0x11E60398u);
  CHECK(alg::adler32(bytes_of("abc")) == alg::adler32(std::string_view{"abc"}));
}

TEST_CASE("nexenne::algorithm::adler32 matches the naive per-byte reference") {
  auto gen{lcg{}};
  // Lengths from 0 up past two NMAX blocks (5552) to exercise the deferred
  // modulo across block boundaries, including the exact boundary.
  for (auto const len :
       {std::size_t{0},
        std::size_t{1},
        std::size_t{55},
        std::size_t{5551},
        std::size_t{5552},
        std::size_t{5553},
        std::size_t{11104},
        std::size_t{12000}}) {
    CAPTURE(len);
    auto const data{random_bytes(gen, len)};
    auto const span{std::span<std::uint8_t const>{data}};
    CHECK(alg::adler32(span) == naive_adler32(span, 1u));
    CHECK(alg::adler32(span, 0xDEADBEEFu) == naive_adler32(span, 0xDEADBEEFu));
  }
}

TEST_CASE("nexenne::algorithm::adler32 seed chaining matches a single pass") {
  auto const text{std::string{"the quick brown fox jumps over the lazy dog, twice over now"}};
  auto const whole{alg::adler32(std::string_view{text})};
  for (auto split{std::size_t{0}}; split <= text.size(); ++split) {
    CAPTURE(split);
    auto const first{alg::adler32(std::string_view{text}.substr(0, split))};
    auto const chained{alg::adler32(std::string_view{text}.substr(split), first)};
    CHECK(chained == whole);
  }
}

TEST_CASE("nexenne::algorithm::adler32 reduces an over-range seed correctly") {
  // The high and low 16-bit halves of the seed can exceed the modulus; both
  // must be reduced before accumulation.
  auto const seed{std::uint32_t{0xFFFFFFFFu}};
  CHECK(alg::adler32(std::span<std::uint8_t const>{}, seed) == naive_adler32({}, seed));
  CHECK(
    alg::adler32(std::string_view{"payload"}, seed) == naive_adler32(bytes_of("payload"), seed)
  );
}

// Modular-sum family: published Fletcher vectors and the generic engine.

static_assert(std::is_same_v<alg::modular_sum_result_t<8>, std::uint16_t>);
static_assert(std::is_same_v<alg::modular_sum_result_t<16>, std::uint32_t>);
static_assert(std::is_same_v<alg::modular_sum_result_t<32>, std::uint64_t>);
static_assert(alg::modular_sum<alg::adler32_spec>(std::span<std::uint8_t const>{}) == 1u);

TEST_CASE("nexenne::algorithm::fletcher published known-answer vectors") {
  CHECK(alg::fletcher16(std::string_view{"abcde"}) == 0xC8F0u);
  CHECK(alg::fletcher16(std::string_view{"abcdef"}) == 0x2057u);
  CHECK(alg::fletcher16(std::string_view{"abcdefgh"}) == 0x0627u);
  CHECK(alg::fletcher32(std::string_view{"abcde"}) == 0xF04FC729u);
  CHECK(alg::fletcher32(std::string_view{"abcdef"}) == 0x56502D2Au);
  CHECK(alg::fletcher32(std::string_view{"abcdefgh"}) == 0xEBE19591u);
  CHECK(alg::fletcher64(std::string_view{"abcde"}) == 0xC8C6C527646362C6ull);
  CHECK(alg::fletcher64(std::string_view{"abcdef"}) == 0xC8C72B276463C8C6ull);
  CHECK(alg::fletcher64(std::string_view{"abcdefgh"}) == 0x312E2B28CCCAC8C6ull);
}

TEST_CASE("nexenne::algorithm::adler32 is the prime-modulus member of the family") {
  // The generic engine with adler32_spec equals the named wrapper.
  CHECK(alg::modular_sum<alg::adler32_spec>(std::string_view{"Wikipedia"}) == 0x11E60398u);
  CHECK(alg::modular_sum<alg::adler32_spec>(bytes_of("abc")) == alg::adler32(bytes_of("abc")));
}

// Differential: the deferred-modulo engine equals the naive per-unit reference
// for every family member, across block boundaries and partial final units.
template <alg::modular_sum_spec Spec>
void modular_sum_matches_naive() {
  using value_type = alg::modular_sum_result_t<Spec.sum_bits>;
  auto gen{lcg{}};
  for (auto const len :
       {std::size_t{0},
        std::size_t{1},
        std::size_t{2},
        std::size_t{3},
        std::size_t{4},
        std::size_t{5},
        std::size_t{7},
        std::size_t{15},
        std::size_t{255},
        std::size_t{5551},
        std::size_t{5552},
        std::size_t{5553},
        std::size_t{11106},
        std::size_t{20003}}) {
    CAPTURE(len);
    auto const data{random_bytes(gen, len)};
    auto const span{std::span<std::uint8_t const>{data}};
    auto const expected{static_cast<value_type>(
      naive_modular_sum(span, Spec.unit_bytes, Spec.sum_bits, Spec.modulus, Spec.init1)
    )};
    CHECK(alg::modular_sum<Spec>(span) == expected);
  }
}

TEST_CASE("nexenne::algorithm::modular_sum matches the naive per-unit reference") {
  SUBCASE("adler32 (byte unit, prime modulus)") {
    modular_sum_matches_naive<alg::adler32_spec>();
  }
  SUBCASE("fletcher16 (byte unit)") {
    modular_sum_matches_naive<alg::fletcher16_spec>();
  }
  SUBCASE("fletcher32 (16-bit unit)") {
    modular_sum_matches_naive<alg::fletcher32_spec>();
  }
  SUBCASE("fletcher64 (32-bit unit)") {
    modular_sum_matches_naive<alg::fletcher64_spec>();
  }
}

TEST_CASE("nexenne::algorithm::fletcher32 seed chaining over whole units") {
  // Continuation is well-defined when the split lands on a unit boundary.
  auto const whole{alg::fletcher32(std::string_view{"abcdefgh"})};
  auto const first{alg::fletcher32(std::string_view{"abcd"})};
  CHECK(alg::modular_sum<alg::fletcher32_spec>(std::string_view{"efgh"}, first) == whole);
}

// CRC: the spec value_type maps to the smallest sufficient unsigned integer.

static_assert(std::is_same_v<alg::crc_spec<8>::value_type, std::uint8_t>);
static_assert(std::is_same_v<alg::crc_spec<16>::value_type, std::uint16_t>);
static_assert(std::is_same_v<alg::crc_spec<32>::value_type, std::uint32_t>);
static_assert(std::is_same_v<alg::crc_spec<7>::value_type, std::uint8_t>);
static_assert(std::is_same_v<alg::crc_spec<24>::value_type, std::uint32_t>);

TEST_CASE("nexenne::algorithm::crc 8-bit presets match the reveng catalogue") {
  constexpr auto s{std::string_view{"123456789"}};
  CHECK(alg::crc<alg::crc8_ccitt_spec>(s) == 0xF4u);
  CHECK(alg::crc<alg::crc8_rohc_spec>(s) == 0xD0u);
  CHECK(alg::crc<alg::crc8_dallas_1wire_spec>(s) == 0xA1u);
  CHECK(alg::crc<alg::crc8_autosar_spec>(s) == 0xDFu);
  CHECK(alg::crc<alg::crc8_j1850_spec>(s) == 0x4Bu);
  CHECK(alg::crc<alg::crc8_icode_spec>(s) == 0x7Eu);
}

TEST_CASE("nexenne::algorithm::crc 16-bit presets match the reveng catalogue") {
  constexpr auto s{std::string_view{"123456789"}};
  CHECK(alg::crc<alg::crc16_xmodem_spec>(s) == 0x31C3u);
  CHECK(alg::crc<alg::crc16_kermit_spec>(s) == 0x2189u);
  CHECK(alg::crc<alg::crc16_modbus_spec>(s) == 0x4B37u);
  CHECK(alg::crc<alg::crc16_usb_spec>(s) == 0xB4C8u);
  CHECK(alg::crc<alg::crc16_x25_spec>(s) == 0x906Eu);
  CHECK(alg::crc<alg::crc16_ibm3740_spec>(s) == 0x29B1u);
}

TEST_CASE("nexenne::algorithm::crc 32-bit presets match the reveng catalogue") {
  constexpr auto s{std::string_view{"123456789"}};
  CHECK(alg::crc<alg::crc32_ieee_spec>(s) == 0xCBF43926u);
  CHECK(alg::crc<alg::crc32c_spec>(s) == 0xE3069283u);
  CHECK(alg::crc<alg::crc32_bzip2_spec>(s) == 0xFC891918u);
  CHECK(alg::crc<alg::crc32_mpeg2_spec>(s) == 0x0376E6E7u);
}

TEST_CASE("nexenne::algorithm::crc empty input yields the preset's empty value") {
  CHECK(alg::crc<alg::crc32_ieee_spec>(std::string_view{""}) == 0u);
  CHECK(alg::crc<alg::crc16_ibm3740_spec>(std::string_view{""}) == 0xFFFFu);
  CHECK(alg::crc<alg::crc8_ccitt_spec>(std::string_view{""}) == 0x00u);
}

TEST_CASE("nexenne::algorithm::crc is constexpr-evaluable") {
  constexpr auto data{std::array<std::uint8_t, 9>{'1', '2', '3', '4', '5', '6', '7', '8', '9'}};
  constexpr auto v{alg::crc<alg::crc32_ieee_spec>(std::span<std::uint8_t const>{data})};
  static_assert(v == 0xCBF43926u);
  CHECK(v == 0xCBF43926u);
}

TEST_CASE("nexenne::algorithm::crc convenience wrappers match their specs") {
  constexpr auto s{std::string_view{"123456789"}};
  CHECK(alg::crc8(s) == 0xF4u);
  CHECK(alg::crc8_1wire(s) == 0xA1u);
  CHECK(alg::crc32(s) == 0xCBF43926u);
  CHECK(alg::crc32c(s) == 0xE3069283u);
  CHECK(alg::crc8(bytes_of("hello")) == alg::crc<alg::crc8_ccitt_spec>(bytes_of("hello")));
  CHECK(alg::crc32(bytes_of("hello")) == alg::crc<alg::crc32_ieee_spec>(bytes_of("hello")));
}

// Specs whose input and output reflection differ. No named preset has
// ref_in != ref_out, so these are the only inputs that reach the asymmetric
// reflect branches in crc<Spec> and crc_ctx::value(). They have no catalogue
// check value, so they are validated against the independent references.
constexpr auto crc16_refin_only{alg::crc_spec<16>{
  .poly = 0x1021, .init = 0xABCD, .ref_in = true, .ref_out = false, .xor_out = 0x1234
}};
constexpr auto crc16_refout_only{alg::crc_spec<16>{
  .poly = 0x8005, .init = 0x0001, .ref_in = false, .ref_out = true, .xor_out = 0xFFFF
}};

// Differential against the bit-serial reference, covering reflected and
// non-reflected specs at every width, over random inputs of many lengths.
template <alg::crc_spec Spec>
void crc_matches_bitwise() {
  auto gen{lcg{}};
  for (auto len{std::size_t{0}}; len <= 300; ++len) {
    CAPTURE(len);
    auto const data{random_bytes(gen, len)};
    auto const span{std::span<std::uint8_t const>{data}};
    CHECK(alg::crc<Spec>(span) == crc_bitwise<Spec>(span));
  }
}

TEST_CASE("nexenne::algorithm::crc matches a bit-serial reference for every model") {
  SUBCASE("crc8 ccitt (non-reflected)") {
    crc_matches_bitwise<alg::crc8_ccitt_spec>();
  }
  SUBCASE("crc8 1wire (reflected)") {
    crc_matches_bitwise<alg::crc8_dallas_1wire_spec>();
  }
  SUBCASE("crc8 autosar (xor-out)") {
    crc_matches_bitwise<alg::crc8_autosar_spec>();
  }
  SUBCASE("crc16 ibm3740 (non-reflected)") {
    crc_matches_bitwise<alg::crc16_ibm3740_spec>();
  }
  SUBCASE("crc16 modbus (reflected)") {
    crc_matches_bitwise<alg::crc16_modbus_spec>();
  }
  SUBCASE("crc16 x25 (reflected, xor-out)") {
    crc_matches_bitwise<alg::crc16_x25_spec>();
  }
  SUBCASE("crc32 ieee (reflected)") {
    crc_matches_bitwise<alg::crc32_ieee_spec>();
  }
  SUBCASE("crc32c (reflected)") {
    crc_matches_bitwise<alg::crc32c_spec>();
  }
  SUBCASE("crc32 bzip2 (non-reflected)") {
    crc_matches_bitwise<alg::crc32_bzip2_spec>();
  }
  SUBCASE("crc32 mpeg2 (non-reflected, no xor-out)") {
    crc_matches_bitwise<alg::crc32_mpeg2_spec>();
  }
  SUBCASE("asymmetric ref_in only (true, false)") {
    crc_matches_bitwise<crc16_refin_only>();
  }
  SUBCASE("asymmetric ref_out only (false, true)") {
    crc_matches_bitwise<crc16_refout_only>();
  }
}

template <alg::crc_spec Spec>
void ctx_matches_oneshot_every_split() {
  auto const text{std::string{"streaming through chunk boundaries of every size, end to end!"}};
  auto const whole{alg::crc<Spec>(std::string_view{text})};
  for (auto split{std::size_t{0}}; split <= text.size(); ++split) {
    CAPTURE(split);
    auto ctx{alg::crc_ctx<Spec>{}};
    ctx.update(std::string_view{text}.substr(0, split));
    ctx.update(std::string_view{text}.substr(split));
    CHECK(ctx.value() == whole);
    // value() does not mutate: a second read agrees.
    CHECK(ctx.value() == whole);
  }
}

TEST_CASE("nexenne::algorithm::crc_ctx streaming matches one-shot across every split") {
  SUBCASE("crc32 ieee (reflected)") {
    ctx_matches_oneshot_every_split<alg::crc32_ieee_spec>();
  }
  SUBCASE("crc16 ibm3740 (non-reflected)") {
    ctx_matches_oneshot_every_split<alg::crc16_ibm3740_spec>();
  }
  SUBCASE("crc16 modbus (reflected)") {
    ctx_matches_oneshot_every_split<alg::crc16_modbus_spec>();
  }
  SUBCASE("crc8 ccitt (non-reflected)") {
    ctx_matches_oneshot_every_split<alg::crc8_ccitt_spec>();
  }
  SUBCASE("crc32 bzip2 (non-reflected)") {
    ctx_matches_oneshot_every_split<alg::crc32_bzip2_spec>();
  }
  SUBCASE("asymmetric ref_in only (true, false)") {
    ctx_matches_oneshot_every_split<crc16_refin_only>();
  }
  SUBCASE("asymmetric ref_out only (false, true)") {
    ctx_matches_oneshot_every_split<crc16_refout_only>();
  }
}

TEST_CASE("nexenne::algorithm::crc_ctx reset reuses the context") {
  auto ctx{alg::crc_ctx<alg::crc32_ieee_spec>{}};
  ctx.update(std::string_view{"first message"});
  CHECK(ctx.value() != alg::crc<alg::crc32_ieee_spec>(std::string_view{""}));
  ctx.reset();
  CHECK(ctx.value() == alg::crc<alg::crc32_ieee_spec>(std::string_view{""}));
  ctx.update(std::string_view{"123456789"});
  CHECK(ctx.value() == 0xCBF43926u);
}

TEST_CASE("nexenne::algorithm::crc_ctx with random chunk boundaries matches one-shot") {
  auto gen{lcg{}};
  auto const data{random_bytes(gen, 1000)};
  auto const whole{alg::crc<alg::crc32c_spec>(std::span<std::uint8_t const>{data})};
  auto ctx{alg::crc_ctx<alg::crc32c_spec>{}};
  auto offset{std::size_t{0}};
  while (offset < data.size()) {
    auto const chunk{(gen.next() % 17u) + 1u};
    auto const take{offset + chunk <= data.size() ? chunk : data.size() - offset};
    ctx.update(std::span<std::uint8_t const>{data.data() + offset, take});
    offset += take;
  }
  CHECK(ctx.value() == whole);
}

TEST_CASE("nexenne::algorithm::crc accepts a custom spec") {
  // CRC-16/CDMA2000 from the catalogue: poly 0xC867, init 0xFFFF, no reflection.
  constexpr auto cdma2000{alg::crc_spec<16>{
    .poly = 0xC867, .init = 0xFFFF, .ref_in = false, .ref_out = false, .xor_out = 0x0000
  }};
  CHECK(alg::crc<cdma2000>(std::string_view{"123456789"}) == 0x4C06u);
  auto gen{lcg{}};
  auto const data{random_bytes(gen, 64)};
  CHECK(
    alg::crc<cdma2000>(std::span<std::uint8_t const>{data})
    == crc_bitwise<cdma2000>(std::span<std::uint8_t const>{data})
  );
}

}  // namespace
