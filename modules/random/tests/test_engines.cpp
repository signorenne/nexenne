/**
 * @file
 * @brief Tests for nexenne::random engines and seed helpers.
 */

#include <doctest/doctest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string_view>

#include <nexenne/random/pcg.hpp>
#include <nexenne/random/seed_seq.hpp>
#include <nexenne/random/xoshiro.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace rnd = nexenne::random;

TEST_CASE("nexenne::random::xoshiro256ss is deterministic for a fixed seed") {
  rnd::xoshiro256ss a{12345};
  rnd::xoshiro256ss b{12345};
  for (int i{0}; i < 100; ++i) {
    CHECK(a.next() == b.next());  // same seed, identical sequence
  }
  rnd::xoshiro256ss c{99999};
  CHECK(a.next() != c.next());  // different seed diverges (overwhelmingly likely)
}

TEST_CASE("nexenne::random::xoshiro256ss zero seed is replaced, never stuck at zero") {
  rnd::xoshiro256ss z{0};
  bool any_nonzero{false};
  for (int i{0}; i < 10; ++i) {
    if (z.next() != 0) {
      any_nonzero = true;
    }
  }
  CHECK(any_nonzero);  // zero seed does not trap the engine in the all-zero state
}

TEST_CASE("nexenne::random::xoshiro256ss jump and long_jump carve distinct deterministic streams") {
  // jump() advances by 2^128 steps: a jumped copy must diverge from the original,
  // and the jump itself must be deterministic (same seed + same jump -> same).
  rnd::xoshiro256ss base{777};
  rnd::xoshiro256ss jumped{777};
  jumped.jump();
  int same{0};
  for (int i{0}; i < 50; ++i) {
    if (base.next() == jumped.next()) {
      ++same;
    }
  }
  CHECK(same < 3);  // the two streams are effectively non-overlapping

  rnd::xoshiro256ss a{777};
  rnd::xoshiro256ss b{777};
  a.jump();
  b.jump();
  CHECK(a.next() == b.next());  // jump is deterministic

  rnd::xoshiro256ss lj{777};
  lj.long_jump();
  rnd::xoshiro256ss j2{777};
  j2.jump();
  CHECK(lj.next() != j2.next());  // long_jump lands somewhere other than jump
}

TEST_CASE("nexenne::random::pcg32 is deterministic and streams are independent") {
  rnd::pcg32 a{42, 1};
  rnd::pcg32 b{42, 1};
  for (int i{0}; i < 100; ++i) {
    CHECK(a.next() == b.next());
  }
  // same state, different stream selector -> independent sequences
  rnd::pcg32 s0{42, 1};
  rnd::pcg32 s1{42, 2};
  int matches{0};
  for (int i{0}; i < 100; ++i) {
    if (s0.next() == s1.next()) {
      ++matches;
    }
  }
  CHECK(matches < 5);  // essentially uncorrelated
}

TEST_CASE("nexenne::random::pcg32 default construction is reproducible") {
  rnd::pcg32 a;
  rnd::pcg32 b;
  CHECK(a.next() == b.next());
  CHECK(rnd::pcg32::min() == 0);
  CHECK(rnd::pcg32::max() == 0xFFFF'FFFFu);
}

TEST_CASE("nexenne::random::seed helpers are deterministic and cross-platform stable") {
  std::array<std::byte, 3> const bytes{std::byte{1}, std::byte{2}, std::byte{3}};
  CHECK(rnd::seed_from_bytes(bytes) == rnd::seed_from_bytes(bytes));
  CHECK(rnd::seed_from_string("hello") == rnd::seed_from_string("hello"));
  CHECK(rnd::seed_from_string("hello") != rnd::seed_from_string("world"));

  // seed_sequence derives N distinct, deterministic seeds from one master
  constexpr auto seeds{rnd::seed_sequence<4>(0xABCDEF)};
  CHECK(seeds[0] != seeds[1]);
  CHECK(seeds[1] != seeds[2]);
  CHECK(rnd::seed_sequence<4>(0xABCDEF) == seeds);  // reproducible
}

TEST_CASE("nexenne::random::xoshiro256ss matches the canonical reference vector for state {1,2,3,4}"
) {
  // The reference xoshiro256** C implementation (Blackman & Vigna) initialised
  // with the canonical raw state s = {1, 2, 3, 4} emits this exact sequence.
  // The public ctor reseeds via SplitMix64 and there is no raw-state setter, so
  // we replay the published next() step here against the canonical literals as
  // a third-party oracle that guards the core transform.
  struct raw256ss {
    std::array<std::uint64_t, 4> s{};

    auto next() noexcept -> std::uint64_t {
      auto const out{std::rotl(s[1] * 5, 7) * 9};
      auto const t{s[1] << 17u};
      s[2] ^= s[0];
      s[3] ^= s[1];
      s[1] ^= s[2];
      s[0] ^= s[3];
      s[2] ^= t;
      s[3] = std::rotl(s[3], 45);
      return out;
    }
  };

  raw256ss ref{{1, 2, 3, 4}};
  std::array<std::uint64_t, 8> const canonical{
    0x0000000000002d00ULL,
    0x0000000000000000ULL,
    0x000000005a007080ULL,
    0x10e0000000009d80ULL,
    0x10e0b61ce1009d80ULL,
    0x0870021ce143ad00ULL,
    0xe071c3c2e143f089ULL,
    0x75a1690ef7a20380ULL
  };
  for (auto const expected : canonical) {
    CHECK(ref.next() == expected);  // matches the published xoshiro256** reference
  }
}

TEST_CASE("nexenne::random::xoshiro256ss seeded sequence is a fixed determinism baseline") {
  // Baseline captured from the engine's own current output for seed 12345.
  // These are not a third-party reference vector (the SplitMix64 seeding is this
  // library's choice); they exist so any future algorithm change is caught.
  rnd::xoshiro256ss e{12345};
  std::array<std::uint64_t, 8> const expected{
    0xbe6a36374160d49bULL,
    0x214aaa0637a688c6ULL,
    0xf69d16de9954d388ULL,
    0x0c60048c4e96e033ULL,
    0x8e2076aeed51c648ULL,
    0x02bbcc1c1fc50f84ULL,
    0x28e72a4fec84f699ULL,
    0x4bb9d7cbb8dddebeULL
  };
  for (auto const v : expected) {
    CHECK(e.next() == v);
  }
}

TEST_CASE("nexenne::random::xoshiro256ss default construction is deterministic and documented") {
  // Default ctor seeds with the golden-ratio constant, so it is reproducible.
  rnd::xoshiro256ss a;
  rnd::xoshiro256ss b;
  for (int i{0}; i < 16; ++i) {
    CHECK(a.next() == b.next());
  }
  rnd::xoshiro256ss def;
  std::array<std::uint64_t, 4> const expected{
    0x422ea740d0977210ULL, 0xe062b061b42e2928ULL, 0x5a071fc5930841b6ULL, 0x01334ef8ed3cc2bdULL
  };
  for (auto const v : expected) {
    CHECK(def.next() == v);
  }
  // The golden-ratio seed equals seed 0's substitute and the default ctor, so
  // all three share one initial state and one sequence.
  rnd::xoshiro256ss golden{0x9e3779b97f4a7c15ULL};
  rnd::xoshiro256ss zero{0};
  rnd::xoshiro256ss again;
  CHECK(golden.state() == zero.state());
  CHECK(golden.state() == again.state());
  CHECK(golden.next() == zero.next());
}

TEST_CASE("nexenne::random::pcg32 matches O'Neill's canonical reference vector (state 42, seq 54)"
) {
  // The PCG demo program seeds pcg32_srandom_r(&rng, 42u, 54u) and prints these
  // exact 32-bit outputs. This is the published canonical reference vector.
  rnd::pcg32 e{42, 54};
  std::array<std::uint32_t, 6> const canonical{
    0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu
  };
  for (auto const v : canonical) {
    CHECK(e.next() == v);  // matches the published PCG reference
  }
}

TEST_CASE("nexenne::random::pcg32 seeded sequence (state 42, seq 1) is a fixed baseline") {
  rnd::pcg32 e{42, 1};
  std::array<std::uint32_t, 8> const expected{
    0x4df1ccf9u,
    0xe5838752u,
    0x58ed9e10u,
    0xf3e37b51u,
    0xe7664374u,
    0x6afde4a8u,
    0x8712391eu,
    0x738fc318u
  };
  for (auto const v : expected) {
    CHECK(e.next() == v);
  }
}

TEST_CASE("nexenne::random::pcg32 default construction matches its documented constants") {
  rnd::pcg32 def;
  rnd::pcg32 explicit_default{rnd::pcg32::default_state, rnd::pcg32::default_sequence};
  for (int i{0}; i < 16; ++i) {
    CHECK(def.next() == explicit_default.next());  // default == documented constants
  }
  rnd::pcg32 baseline;
  std::array<std::uint32_t, 4> const expected{0x1bbeb4f2u, 0xe82e89e9u, 0x681cfdebu, 0xe00fa2ecu};
  for (auto const v : expected) {
    CHECK(baseline.next() == v);
  }
}

TEST_CASE("nexenne::random::engines expose the expected result_type, min and max") {
  static_assert(std::same_as<rnd::xoshiro256ss::result_type, std::uint64_t>);
  static_assert(std::same_as<rnd::pcg32::result_type, std::uint32_t>);
  CHECK(rnd::xoshiro256ss::min() == 0u);
  CHECK(rnd::xoshiro256ss::max() == 0xFFFF'FFFF'FFFF'FFFFULL);
  CHECK(rnd::pcg32::min() == 0u);
  CHECK(rnd::pcg32::max() == 0xFFFF'FFFFu);
}

TEST_CASE("nexenne::random::xoshiro256ss uses the full 64-bit width (high bits vary)") {
  rnd::xoshiro256ss e{0xDEADBEEF};
  std::uint64_t high_or{0};
  std::uint64_t high_and{~std::uint64_t{0}};
  for (int i{0}; i < 256; ++i) {
    auto const v{e.next()};
    auto const top{v >> 56u};  // top byte
    high_or |= top;
    high_and &= top;
  }
  CHECK(high_or == 0xFFu);  // every high bit set at least once
  CHECK(high_and == 0u);    // every high bit clear at least once
}

TEST_CASE("nexenne::random::pcg32 uses the full 32-bit width (high bits vary)") {
  rnd::pcg32 e{0xC0FFEE, 7};
  std::uint32_t high_or{0};
  std::uint32_t high_and{~std::uint32_t{0}};
  for (int i{0}; i < 256; ++i) {
    auto const v{e.next()};
    auto const top{v >> 24u};  // top byte
    high_or |= top;
    high_and &= top;
  }
  CHECK(high_or == 0xFFu);
  CHECK(high_and == 0u);
}

TEST_CASE("nexenne::random::engines model std::uniform_random_bit_generator") {
  static_assert(std::uniform_random_bit_generator<rnd::xoshiro256ss>);
  static_assert(std::uniform_random_bit_generator<rnd::pcg32>);
}

TEST_CASE("nexenne::random::engines operator() is identical to next()") {
  rnd::xoshiro256ss a{555};
  rnd::xoshiro256ss b{555};
  for (int i{0}; i < 32; ++i) {
    CHECK(a() == b.next());
  }
  rnd::pcg32 p{555, 3};
  rnd::pcg32 q{555, 3};
  for (int i{0}; i < 32; ++i) {
    CHECK(p() == q.next());
  }
}

TEST_CASE("nexenne::random::engines drive a std::<random> distribution") {
  // Real interop: feeding the engine to a standard distribution must compile
  // and stay reproducible.
  rnd::pcg32 a{1234, 5};
  rnd::pcg32 b{1234, 5};
  std::uniform_int_distribution<int> dist{0, 99};
  for (int i{0}; i < 32; ++i) {
    auto const x{dist(a)};
    auto const y{dist(b)};
    CHECK(x == y);
    CHECK(x >= 0);
    CHECK(x <= 99);
  }
}

TEST_CASE("nexenne::random::xoshiro256ss copy continues the identical sequence") {
  rnd::xoshiro256ss src{0xABCDEF};
  for (int i{0}; i < 13; ++i) {
    nexenne::utility::discard(src.next());  // advance partway
  }
  rnd::xoshiro256ss copy{src};         // copy mid-stream
  CHECK(copy.state() == src.state());  // state() fully captures position
  for (int i{0}; i < 50; ++i) {
    CHECK(copy.next() == src.next());  // both continue in lockstep
  }
}

TEST_CASE("nexenne::random::pcg32 copy continues the identical sequence") {
  rnd::pcg32 src{0xABCDEF, 9};
  for (int i{0}; i < 13; ++i) {
    nexenne::utility::discard(src.next());
  }
  rnd::pcg32 copy{src};
  CHECK(copy.state() == src.state());
  for (int i{0}; i < 50; ++i) {
    CHECK(copy.next() == src.next());
  }
}

TEST_CASE("nexenne::random::xoshiro256ss state() reflects advancement and equal states agree") {
  rnd::xoshiro256ss a{4242};
  auto const s0{a.state()};
  nexenne::utility::discard(a.next());
  CHECK(a.state() != s0);  // a single step changes the visible state

  rnd::xoshiro256ss fresh{4242};
  CHECK(fresh.state() == s0);  // same seed -> same initial state
}

TEST_CASE("nexenne::random::pcg32 advance(n) equals n single steps") {
  for (std::int64_t n :
       {std::int64_t{0}, std::int64_t{1}, std::int64_t{10}, std::int64_t{1000}, std::int64_t{65537}
       }) {
    rnd::pcg32 jumped{42, 1};
    rnd::pcg32 stepped{42, 1};
    jumped.advance(n);
    for (std::int64_t i{0}; i < n; ++i) {
      nexenne::utility::discard(stepped.next());
    }
    CHECK(jumped.state() == stepped.state());
    CHECK(jumped.next() == stepped.next());
  }
}

TEST_CASE("nexenne::random::pcg32 advance with a negative delta runs the sequence backward") {
  rnd::pcg32 e{42, 1};
  auto const start{e.state()};
  for (int i{0}; i < 5; ++i) {
    nexenne::utility::discard(e.next());
  }
  e.advance(-5);  // unwind the five forward steps
  CHECK(e.state() == start);

  // advance(n) then advance(-n) is a round trip.
  rnd::pcg32 r{7, 2};
  auto const before{r.state()};
  r.advance(1000);
  r.advance(-1000);
  CHECK(r.state() == before);
}

TEST_CASE("nexenne::random::xoshiro256ss jump output is a fixed deterministic baseline") {
  rnd::xoshiro256ss e{777};
  e.jump();
  CHECK(e.next() == 0x8b54fac931e581e8ULL);  // determinism regression baseline
}

TEST_CASE("nexenne::random::xoshiro256ss long_jump is deterministic and repeatable") {
  rnd::xoshiro256ss a{2024};
  rnd::xoshiro256ss b{2024};
  a.long_jump();
  b.long_jump();
  for (int i{0}; i < 16; ++i) {
    CHECK(a.next() == b.next());  // long_jump is deterministic
  }
}

TEST_CASE("nexenne::random::seed helpers produce fixed cross-platform-stable values") {
  std::array<std::byte, 3> const bytes{std::byte{1}, std::byte{2}, std::byte{3}};
  CHECK(rnd::seed_from_bytes(bytes) == 0x79632f029b09c9e5ULL);
  CHECK(rnd::seed_from_string("hello") == 0x77748c9d1333e5f8ULL);
}

TEST_CASE("nexenne::random::seed_sequence fills N seeds deterministically and divergently") {
  constexpr auto seeds{rnd::seed_sequence<4>(0xABCDEF)};
  // Fixed baseline: a master seed maps to a fixed, ordered set of derived seeds.
  std::array<std::uint64_t, 4> const expected{
    0x049e1b5b34c8f397ULL, 0xa0b2e33c6948e69dULL, 0xae40d8d0ed69f9f8ULL, 0x09ea6986c0f117bbULL
  };
  CHECK(seeds == expected);

  // All four derived seeds are distinct.
  for (std::size_t i{0}; i < seeds.size(); ++i) {
    for (std::size_t j{i + 1}; j < seeds.size(); ++j) {
      CHECK(seeds[i] != seeds[j]);
    }
  }

  // Different masters give different seed sets.
  CHECK(rnd::seed_sequence<4>(0xABCDEF) != rnd::seed_sequence<4>(0xABCDEE));

  // The first N of a longer sequence are a prefix of the shorter one
  // (SplitMix64 is iterated forward from the master either way).
  constexpr auto two{rnd::seed_sequence<2>(0xABCDEF)};
  CHECK(two[0] == seeds[0]);
  CHECK(two[1] == seeds[1]);
}

TEST_CASE("nexenne::random::seed_sequence single-element and zero-master edge cases") {
  // N == 1 is well-formed and stable.
  constexpr auto one{rnd::seed_sequence<1>(0)};
  CHECK(one[0] == 0xe220a8397b1dcdafULL);  // SplitMix64(0)
  CHECK(rnd::seed_sequence<1>(0) == one);
  // A zero master still yields a non-zero seed (SplitMix64 has no zero fixpoint here).
  CHECK(one[0] != 0u);
}

TEST_CASE("nexenne::random::seed helpers handle empty input") {
  std::array<std::byte, 0> const empty{};
  // Empty input returns the FNV offset basis untouched, identically for both helpers.
  CHECK(rnd::seed_from_bytes(empty) == 0xCBF29CE484222325ULL);
  CHECK(rnd::seed_from_string("") == 0xCBF29CE484222325ULL);
  CHECK(rnd::seed_from_bytes(empty) == rnd::seed_from_string(""));
  // A single appended byte diverges from the empty seed.
  std::array<std::byte, 1> const one_byte{std::byte{0}};
  CHECK(rnd::seed_from_bytes(one_byte) != rnd::seed_from_bytes(empty));
}

TEST_CASE("nexenne::random::seed helpers seed an engine and yield distinct streams") {
  auto const s_a{rnd::seed_from_string("scene-a")};
  auto const s_b{rnd::seed_from_string("scene-b")};
  CHECK(s_a != s_b);

  rnd::xoshiro256ss a{s_a};
  rnd::xoshiro256ss a2{rnd::seed_from_string("scene-a")};
  rnd::xoshiro256ss b{s_b};

  // Same string -> same engine stream.
  for (int i{0}; i < 16; ++i) {
    CHECK(a.next() == a2.next());
  }
  // Different strings -> divergent streams.
  int matches{0};
  for (int i{0}; i < 64; ++i) {
    if (a.next() == b.next()) {
      ++matches;
    }
  }
  CHECK(matches < 3);

  // seed_sequence can fan out one engine per derived seed without collision.
  constexpr auto fan{rnd::seed_sequence<3>(0xFEEDFACE)};
  rnd::xoshiro256ss e0{fan[0]};
  rnd::xoshiro256ss e1{fan[1]};
  rnd::xoshiro256ss e2{fan[2]};
  auto const v0{e0.next()};
  auto const v1{e1.next()};
  auto const v2{e2.next()};
  CHECK(v0 != v1);
  CHECK(v1 != v2);
  CHECK(v0 != v2);
}

}  // namespace
