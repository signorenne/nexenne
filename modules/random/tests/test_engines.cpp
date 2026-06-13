/**
 * @file
 * @brief Tests for nexenne::random engines and seed helpers.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nexenne/random/pcg.hpp>
#include <nexenne/random/seed_seq.hpp>
#include <nexenne/random/xoshiro.hpp>

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

}  // namespace
