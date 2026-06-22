/**
 * @file
 * @brief Seedable RNG engines: reproducible sequences, parallel streams, seeds.
 *
 * Both engines are seedable and deterministic, so a fixed seed replays the exact
 * same sequence, which is what makes simulations and tests reproducible. This
 * tour covers: reproducibility, the standard-library interop interface, pcg's
 * O(log n) jump-ahead, xoshiro's constant-time stream splitting, and the three
 * seed-derivation helpers (string, bytes, and a master-to-N fan-out).
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>

#include <nexenne/random/pcg.hpp>
#include <nexenne/random/seed_seq.hpp>
#include <nexenne/random/uniform.hpp>
#include <nexenne/random/xoshiro.hpp>

namespace {

namespace rnd = nexenne::random;

}  // namespace

auto main() -> int {
  // --- Reproducibility -------------------------------------------------------
  // A fixed (state, sequence) pair fully determines the stream, so two engines
  // seeded identically agree forever.
  rnd::pcg32 a{42, 1};
  rnd::pcg32 b{42, 1};
  std::println(
    "pcg32 same seed, first 3 equal: {} {} {}",
    a.next() == b.next(),
    a.next() == b.next(),
    a.next() == b.next()
  );

  // The "sequence" argument selects an independent output *stream* from the same
  // state: same state, different stream -> uncorrelated output.
  rnd::pcg32 s0{42, 1};
  rnd::pcg32 s1{42, 2};
  std::println("pcg32 same state, different stream differs: {}", s0.next() != s1.next());

  // --- Standard-library interop ----------------------------------------------
  // Every engine exposes result_type / min() / max() / operator(), so it models
  // std::uniform_random_bit_generator and can drive <random> if you want it
  // (though the portable samplers in uniform.hpp are the reason this module
  // exists). operator() is just next().
  rnd::pcg32 interop{7, 1};
  std::println(
    "pcg32 range [{}, {}], operator() works: {}",
    rnd::pcg32::min(),
    rnd::pcg32::max(),
    interop() != interop()  // two draws, almost surely distinct
  );

  // --- pcg jump-ahead --------------------------------------------------------
  // advance(n) skips n outputs in O(log n) via the LCG jump formula - far
  // cheaper than calling next() n times. Advancing one engine by k must land on
  // the same value the other reaches after k manual draws.
  rnd::pcg32 jumper{99, 1};
  rnd::pcg32 walker{99, 1};
  jumper.advance(1000);
  for (int i{0}; i < 1000; ++i) {
    static_cast<void>(walker.next());
  }
  std::println("pcg32 advance(1000) == 1000x next(): {}", jumper.next() == walker.next());

  // --- xoshiro parallel streams ----------------------------------------------
  // For multi-threaded work you want non-overlapping substreams. Copy the
  // engine and jump() each copy: jump() advances by 2^128 draws in constant
  // time, so two workers cannot collide for any realistic workload.
  rnd::xoshiro256ss base{0xABCDEF};
  rnd::xoshiro256ss worker0{base};
  rnd::xoshiro256ss worker1{base};
  worker1.jump();
  std::println("xoshiro jump() gives distinct stream: {}", worker0.next() != worker1.next());

  // --- Seed derivation -------------------------------------------------------
  // seed_sequence<N> fans one master seed out into N independent sub-seeds via
  // SplitMix64 - one per thread/subsystem, no correlation, fully reproducible.
  constexpr auto seeds{rnd::seed_sequence<3>(0xC0FFEE)};
  std::println(
    "3 worker seeds distinct: {}",
    seeds[0] != seeds[1] && seeds[1] != seeds[2] && seeds[0] != seeds[2]
  );

  // Seed from a human-readable string: same name -> same world, on every
  // toolchain (unlike std::seed_seq, whose mixing is implementation-defined).
  rnd::xoshiro256ss world{rnd::seed_from_string("level-1")};
  std::println("seeded from \"level-1\", first draw nonzero: {}", world.next() != 0);

  // seed_from_bytes hashes arbitrary binary input (e.g. a save-file header) with
  // the identical stable mixing, and equal strings/bytes map to equal seeds.
  constexpr std::array<std::byte, 5> blob{
    std::byte{'l'},
    std::byte{'e'},
    std::byte{'v'},
    std::byte{'1'},
    std::byte{0},
  };
  auto const byte_seed{rnd::seed_from_bytes(blob)};
  std::println("seed_from_bytes deterministic: {}", byte_seed == rnd::seed_from_bytes(blob));

  // five d6 rolls off a string-seeded engine, just to show the whole chain.
  rnd::xoshiro256ss dungeon{rnd::seed_from_string("crypt")};
  std::print("five d6 (seed \"crypt\"):");
  for (int i{0}; i < 5; ++i) {
    std::print(" {}", rnd::uniform_int(dungeon, 1, 6));
  }
  std::println("");
  // pcg32 same seed, first 3 equal: true true true
  // pcg32 same state, different stream differs: true
  // pcg32 advance(1000) == 1000x next(): true
  // xoshiro jump() gives distinct stream: true
  // 3 worker seeds distinct: true
  // seeded from "level-1", first draw nonzero: true
  // seed_from_bytes deterministic: true
  return 0;
}
