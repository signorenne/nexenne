/**
 * @file
 * @brief Tests for nexenne::algorithm non-cryptographic hashes.
 *
 * Validates against canonical known-answer tests where they exist (the official
 * FNV-1a vectors, the xxHash empty-input vectors, and the SMHasher verification
 * values for MurmurHash3), then exercises behavioural properties that catch
 * byte-handling and mixing bugs: determinism, avalanche (~half the output bits
 * flip for a one-bit input change), single-byte distinctness, length and seed
 * sensitivity, span/string-view equivalence, and (for the streaming hashes)
 * that the context matches the one-shot result across every chunk boundary.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/algorithm/hash/fnv.hpp>
#include <nexenne/algorithm/hash/murmur.hpp>
#include <nexenne/algorithm/hash/xxhash.hpp>

namespace {

namespace alg = nexenne::algorithm;

[[nodiscard]] auto bytes_of(std::string_view const s) -> std::span<std::uint8_t const> {
  return {reinterpret_cast<std::uint8_t const*>(s.data()), s.size()};
}

// Number of differing bits between two hash results (uint or 128-bit pair).
[[nodiscard]] auto bit_diff(std::uint32_t const a, std::uint32_t const b) -> int {
  return std::popcount(a ^ b);
}

[[nodiscard]] auto bit_diff(std::uint64_t const a, std::uint64_t const b) -> int {
  return std::popcount(a ^ b);
}

[[nodiscard]] auto
bit_diff(std::array<std::uint64_t, 2> const a, std::array<std::uint64_t, 2> const b) -> int {
  return std::popcount(a[0] ^ b[0]) + std::popcount(a[1] ^ b[1]);
}

// A simple deterministic byte source for building test inputs.
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

// FNV-1a: full official known-answer vectors

static_assert(alg::fnv1a<32>(std::string_view{""}) == 0x811C9DC5u);
static_assert(alg::fnv1a<64>(std::string_view{""}) == 0xCBF29CE484222325ull);
static_assert(alg::fnv1a<32>(std::string_view{"a"}) == 0xE40C292Cu);
static_assert(alg::fnv1a<64>(std::string_view{"a"}) == 0xAF63DC4C8601EC8Cull);
static_assert(alg::fnv1a<32>(std::string_view{"foobar"}) == 0xBF9CF968u);
static_assert(alg::fnv1a<64>(std::string_view{"foobar"}) == 0x85944171F73967E8ull);

TEST_CASE("nexenne::algorithm::fnv1a official known-answer vectors (runtime)") {
  CHECK(alg::fnv1a<32>(std::string_view{""}) == 0x811C9DC5u);
  CHECK(alg::fnv1a<64>(std::string_view{""}) == 0xCBF29CE484222325ull);
  CHECK(alg::fnv1a<32>(std::string_view{"a"}) == 0xE40C292Cu);
  CHECK(alg::fnv1a<64>(std::string_view{"a"}) == 0xAF63DC4C8601EC8Cull);
  CHECK(alg::fnv1a<32>(std::string_view{"foobar"}) == 0xBF9CF968u);
  CHECK(alg::fnv1a<64>(std::string_view{"foobar"}) == 0x85944171F73967E8ull);
  // The span overload agrees with the string-view overload.
  CHECK(alg::fnv1a<64>(bytes_of("foobar")) == alg::fnv1a<64>(std::string_view{"foobar"}));
}

TEST_CASE("nexenne::algorithm::fnv1a_ctx streaming matches one-shot across split points") {
  auto const text{std::string{"the quick brown fox jumps over the lazy dog"}};
  auto const oneshot{alg::fnv1a<64>(std::string_view{text})};
  for (auto split{std::size_t{0}}; split <= text.size(); ++split) {
    CAPTURE(split);
    auto ctx{alg::fnv1a_ctx<64>{}};
    ctx.update(std::string_view{text}.substr(0, split));
    ctx.update(std::string_view{text}.substr(split));
    CHECK(ctx.value() == oneshot);
  }
  // Default-constructed context equals the empty-input one-shot.
  CHECK(alg::fnv1a_ctx<64>{}.value() == alg::fnv1a<64>(std::string_view{""}));
  // Seeded context honours its seed; reset restores it.
  auto ctx{alg::fnv1a_ctx<32>{0x12345678u}};
  CHECK(ctx.value() == 0x12345678u);
  ctx.update(bytes_of("x"));
  ctx.reset(0x12345678u);
  CHECK(ctx.value() == 0x12345678u);
}

// xxHash: published known-answer vectors. The empty input pins seeding and
// finalization; "abc" and "abcd" pin the multi-byte body and tail paths.

TEST_CASE("nexenne::algorithm::xxhash published known-answer vectors") {
  CHECK(alg::xxhash<32>(std::span<std::uint8_t const>{}) == 0x02CC5D05u);
  CHECK(alg::xxhash<64>(std::span<std::uint8_t const>{}) == 0xEF46DB3751D8E999ull);
  CHECK(alg::xxhash<32>(std::string_view{""}) == 0x02CC5D05u);
  CHECK(alg::xxhash<64>(std::string_view{""}) == 0xEF46DB3751D8E999ull);
  CHECK(alg::xxhash<32>(std::string_view{"abc"}) == 0x32D153FFu);
  CHECK(alg::xxhash<64>(std::string_view{"abc"}) == 0x44BC2CF5AD770999ull);
  CHECK(alg::xxhash<32>(std::string_view{"abcd"}) == 0xA3643705u);
  CHECK(alg::xxhash<64>(std::string_view{"abcd"}) == 0xDE0327B0D25D92CCull);
  // The span overload agrees with the string-view overload.
  CHECK(alg::xxhash<64>(bytes_of("abc")) == alg::xxhash<64>(std::string_view{"abc"}));
}

// MurmurHash3: SMHasher verification values
// SMHasher hashes keys of length 0..255 (key[i] == i) each with seed 256-len,
// stores each little-endian result end to end, then hashes that buffer with
// seed 0; the low 32 bits are the published verification value.

TEST_CASE("nexenne::algorithm::murmur3<32> matches the SMHasher verification value") {
  auto key{std::array<std::uint8_t, 256>{}};
  auto hashes{std::vector<std::uint8_t>{}};
  hashes.reserve(256 * 4);
  for (auto len{0}; len < 256; ++len) {
    key[static_cast<std::size_t>(len)] = static_cast<std::uint8_t>(len);
    auto const h{alg::murmur3<32>(
      std::span<std::uint8_t const>{key.data(), static_cast<std::size_t>(len)},
      static_cast<std::uint32_t>(256 - len)
    )};
    for (auto shift{0}; shift < 32; shift += 8) {
      hashes.push_back(static_cast<std::uint8_t>(h >> shift));
    }
  }
  auto const verification{alg::murmur3<32>(std::span<std::uint8_t const>{hashes}, 0u)};
  CHECK(verification == 0xB0F57EE3u);
}

TEST_CASE("nexenne::algorithm::murmur3<128> matches the SMHasher verification value") {
  auto key{std::array<std::uint8_t, 256>{}};
  auto hashes{std::vector<std::uint8_t>{}};
  hashes.reserve(256 * 16);
  for (auto len{0}; len < 256; ++len) {
    key[static_cast<std::size_t>(len)] = static_cast<std::uint8_t>(len);
    auto const h{alg::murmur3<128>(
      std::span<std::uint8_t const>{key.data(), static_cast<std::size_t>(len)},
      static_cast<std::uint64_t>(256 - len)
    )};
    for (auto const word : h) {
      for (auto shift{0}; shift < 64; shift += 8) {
        hashes.push_back(static_cast<std::uint8_t>(word >> shift));
      }
    }
  }
  auto const verification{alg::murmur3<128>(std::span<std::uint8_t const>{hashes}, 0ull)};
  CHECK(static_cast<std::uint32_t>(verification[0]) == 0x6384BA69u);
}

TEST_CASE("nexenne::algorithm::murmur3<32> of empty input is zero") {
  CHECK(alg::murmur3<32>(std::span<std::uint8_t const>{}, 0u) == 0u);
}

// Behavioural properties, run against each hash

// Determinism, length sensitivity, seed sensitivity, span/string equivalence.
template <typename Fn>
void behavioural_core(Fn hash) {
  auto gen{lcg{}};
  auto buf{std::vector<std::uint8_t>{}};
  for (auto len{0}; len <= 130; ++len) {  // covers all stripe/tail boundaries
    CAPTURE(len);
    // Determinism: two calls agree.
    auto const span{std::span<std::uint8_t const>{buf}};
    CHECK(hash(span, 0) == hash(span, 0));
    // Seed sensitivity: a different seed almost always changes the hash.
    if (len > 0) {
      CHECK(hash(span, 0) != hash(span, 0x9E3779B9u));
    }
    // Appending a byte changes the hash (length + new content matters).
    auto longer{buf};
    longer.push_back(gen.byte());
    CHECK(hash(std::span<std::uint8_t const>{longer}, 0) != hash(span, 0));
    buf.push_back(gen.byte());
  }
}

// Avalanche: flipping any one input bit flips ~half the output bits.
template <typename Fn>
void avalanche(Fn hash, int const out_bits) {
  auto gen{lcg{}};
  auto total_flips{std::int64_t{0}};
  auto trials{std::int64_t{0}};
  for (auto trial{0}; trial < 64; ++trial) {
    auto buf{std::vector<std::uint8_t>{}};
    for (auto i{0}; i < 24; ++i) {
      buf.push_back(gen.byte());
    }
    auto const base{hash(std::span<std::uint8_t const>{buf}, 0)};
    for (auto bit{std::size_t{0}}; bit < buf.size() * 8; ++bit) {
      auto flipped{buf};
      flipped[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
      total_flips += bit_diff(base, hash(std::span<std::uint8_t const>{flipped}, 0));
      ++trials;
    }
  }
  auto const mean_ratio{static_cast<double>(total_flips) / static_cast<double>(trials * out_bits)};
  CAPTURE(mean_ratio);
  CHECK(mean_ratio > 0.4);
  CHECK(mean_ratio < 0.6);
}

// All 256 single-byte inputs produce distinct hashes (no byte is dropped).
template <typename Fn>
void single_byte_distinct(Fn hash) {
  using result_type = decltype(hash(std::span<std::uint8_t const>{}));
  auto seen{std::set<result_type>{}};
  for (auto b{0}; b < 256; ++b) {
    auto const one{std::array<std::uint8_t, 1>{static_cast<std::uint8_t>(b)}};
    seen.insert(hash(std::span<std::uint8_t const>{one}));
  }
  CHECK(seen.size() == 256);
}

TEST_CASE("nexenne::algorithm hashes: behavioural core (determinism, length, seed)") {
  behavioural_core([](std::span<std::uint8_t const> s, std::uint32_t seed) {
    return alg::fnv1a<32>(s, seed == 0 ? alg::fnv1a_offset<32> : seed);
  });
  behavioural_core([](std::span<std::uint8_t const> s, std::uint32_t seed) {
    return alg::xxhash<32>(s, seed);
  });
  behavioural_core([](std::span<std::uint8_t const> s, std::uint32_t seed) {
    return alg::xxhash<64>(s, seed);
  });
  behavioural_core([](std::span<std::uint8_t const> s, std::uint32_t seed) {
    return alg::murmur3<32>(s, seed);
  });
  behavioural_core([](std::span<std::uint8_t const> s, std::uint32_t seed) {
    return alg::murmur3<128>(s, seed);
  });
}

TEST_CASE("nexenne::algorithm hashes: avalanche (~half the output bits flip)") {
  avalanche(
    [](std::span<std::uint8_t const> s, std::uint32_t) { return alg::xxhash<32>(s, 0); }, 32
  );
  avalanche(
    [](std::span<std::uint8_t const> s, std::uint32_t) { return alg::xxhash<64>(s, 0); }, 64
  );
  avalanche(
    [](std::span<std::uint8_t const> s, std::uint32_t) { return alg::murmur3<32>(s, 0); }, 32
  );
  avalanche(
    [](std::span<std::uint8_t const> s, std::uint32_t) { return alg::murmur3<128>(s, 0); }, 128
  );
  // FNV-1a is intentionally not avalanche-tested: it is a documented
  // weak-avalanche hash (the last input byte diffuses through a single
  // multiply), and the official KATs already validate it exactly.
}

TEST_CASE("nexenne::algorithm hashes: every single-byte input is distinct") {
  single_byte_distinct([](std::span<std::uint8_t const> s) { return alg::xxhash<32>(s, 0); });
  single_byte_distinct([](std::span<std::uint8_t const> s) { return alg::xxhash<64>(s, 0); });
  single_byte_distinct([](std::span<std::uint8_t const> s) { return alg::murmur3<32>(s, 0); });
  single_byte_distinct([](std::span<std::uint8_t const> s) { return alg::murmur3<128>(s, 0); });
  single_byte_distinct([](std::span<std::uint8_t const> s) { return alg::fnv1a<32>(s); });
}

// Streaming xxHash must match one-shot for every input length and chunk size.
template <std::size_t Width>
void xxhash_streaming_matches_oneshot() {
  auto gen{lcg{}};
  for (auto len{std::size_t{0}}; len <= 80; ++len) {
    auto buf{std::vector<std::uint8_t>{}};
    for (auto i{std::size_t{0}}; i < len; ++i) {
      buf.push_back(gen.byte());
    }
    auto const oneshot{alg::xxhash<Width>(std::span<std::uint8_t const>{buf}, 0)};
    // Feed in chunks of every size from 1 to len (exercises buffer fills,
    // whole-stripe consumption, and partial-stripe carryover).
    for (auto chunk{std::size_t{1}}; chunk <= len || chunk == 1; ++chunk) {
      CAPTURE(len);
      CAPTURE(chunk);
      auto ctx{alg::xxhash_ctx<Width>{}};
      for (auto off{std::size_t{0}}; off < len; off += chunk) {
        auto const take{std::min(chunk, len - off)};
        ctx.update(std::span<std::uint8_t const>{buf.data() + off, take});
      }
      CHECK(ctx.value() == oneshot);
      if (len == 0) {
        break;
      }
    }
  }
}

TEST_CASE("nexenne::algorithm::xxhash_ctx streaming matches one-shot (all lengths and chunks)") {
  xxhash_streaming_matches_oneshot<32>();
  xxhash_streaming_matches_oneshot<64>();
  // value() is repeatable and does not consume state.
  auto ctx{alg::xxhash_ctx<64>{}};
  ctx.update(bytes_of("hello world"));
  auto const v{ctx.value()};
  CHECK(ctx.value() == v);
  ctx.update(bytes_of(" more"));
  CHECK(ctx.value() != v);
}

TEST_CASE("nexenne::algorithm hashes: distribution of low bits is roughly uniform") {
  // Hash 0..N-1 (as 4 little-endian bytes) and bucket the low byte; a structural
  // bias would skew the chi-square well past the threshold.
  constexpr auto n{200000};
  constexpr auto buckets{256};
  auto counts{std::array<int, buckets>{}};
  for (auto i{0}; i < n; ++i) {
    auto const key{std::array<std::uint8_t, 4>{
      static_cast<std::uint8_t>(i),
      static_cast<std::uint8_t>(i >> 8),
      static_cast<std::uint8_t>(i >> 16),
      static_cast<std::uint8_t>(i >> 24),
    }};
    auto const h{alg::xxhash<64>(std::span<std::uint8_t const>{key}, 0)};
    ++counts[h & 0xFFu];
  }
  auto const expected{static_cast<double>(n) / buckets};
  auto chi2{0.0};
  for (auto const c : counts) {
    auto const d{static_cast<double>(c) - expected};
    chi2 += d * d / expected;
  }
  CAPTURE(chi2);
  CHECK(chi2 < 400.0);  // 255 dof: ~310 at p=0.999; comfortably below 400
}

TEST_CASE("nexenne::algorithm::fnv1a seeding and empty-input identity") {
  auto const data{std::string_view{"nexenne"}};
  // Distinct seeds give distinct results; equal seeds are reproducible.
  CHECK(alg::fnv1a<32>(data, 0x11111111u) != alg::fnv1a<32>(data, 0x22222222u));
  CHECK(alg::fnv1a<32>(data, 0x11111111u) == alg::fnv1a<32>(data, 0x11111111u));
  CHECK(alg::fnv1a<64>(data, 1ull) != alg::fnv1a<64>(data, 2ull));
  // An empty input returns the seed unchanged (offset basis by default).
  CHECK(alg::fnv1a<32>(std::string_view{""}, 7u) == 7u);
  CHECK(alg::fnv1a<64>(std::string_view{""}, 0x1234567890ABCDEFull) == 0x1234567890ABCDEFull);
  CHECK(alg::fnv1a<32>(std::string_view{""}) == alg::fnv1a_offset<32>);
}

}  // namespace
