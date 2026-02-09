/**
 * @file
 * @brief Seedable RNG engines: reproducible sequences and independent streams.
 *
 * Both engines are seedable and deterministic, so a fixed seed replays the exact
 * same sequence, which is what makes simulations and tests reproducible.
 */

#include <cstdint>
#include <print>

#include <nexenne/random/pcg.hpp>
#include <nexenne/random/seed_seq.hpp>
#include <nexenne/random/xoshiro.hpp>

namespace {

namespace rnd = nexenne::random;

}  // namespace

auto main() -> int {
  // A fixed seed replays the same sequence every run.
  rnd::pcg32 a{42, 1};
  rnd::pcg32 b{42, 1};
  std::println(
    "pcg32 same seed, first 3 equal: {} {} {}",
    a.next() == b.next(),
    a.next() == b.next(),
    a.next() == b.next()
  );

  // Derive one independent seed per worker from a single master seed.
  constexpr auto seeds{rnd::seed_sequence<3>(0xC0FFEE)};
  std::println(
    "3 worker seeds distinct: {}",
    seeds[0] != seeds[1] && seeds[1] != seeds[2] && seeds[0] != seeds[2]
  );

  // Seed an engine from a human-readable string.
  rnd::xoshiro256ss world{rnd::seed_from_string("level-1")};
  std::println("seeded from \"level-1\", first draw nonzero: {}", world.next() != 0);
  // pcg32 same seed, first 3 equal: true true true
  // 3 worker seeds distinct: true
  // seeded from "level-1", first draw nonzero: true
  return 0;
}
