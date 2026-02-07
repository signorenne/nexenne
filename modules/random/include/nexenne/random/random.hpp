#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::random module.
 *
 * Fast, portable, reproducible non-cryptographic random number
 * generation. Where \c \<random\> exists in the standard library,
 * the library deliberately does NOT re-export it, instead this
 * module provides:
 *
 *   - \c pcg32 : 64-bit state, 32-bit output. The default
 *     general-purpose engine.
 *   - \c xoshiro256ss : 256-bit state, 64-bit output. Faster
 *     than PCG64 with parallel-stream support via \c jump().
 *   - \c uniform_int / \c uniform_real / \c bernoulli :
 *     portable, bias-free range samplers. \c std::uniform_*
 *     is not portable across libstdc++/libc++ for the same
 *     engine + spec.
 *   - \c shuffle / \c reservoir_sample / \c weighted_choice :
 *     range-aware sampling primitives.
 *
 * Quick start:
 *
 * \code
 * #include <nexenne/random/random.hpp>
 *
 * namespace rnd = nexenne::random;
 *
 * int main() {
 *     auto rng{rnd::pcg32{42, 1}};
 *     auto const die  {rnd::uniform_int<int>(rng, 1, 6)};
 *     auto const angle{rnd::uniform_real(rng) * 6.28318};
 *
 *     auto deck{std::vector<int>{1,2,3,4,5,6,7,8,9,10}};
 *     rnd::shuffle(deck, rng);
 *
 *     auto const pick{rnd::reservoir_sample(deck, 3, rng)};
 * }
 * \endcode
 *
 * \warning Not cryptographically secure. For unguessable bits
 *          (tokens, keys, nonces), use the OS RNG or a
 *          cipher-based PRNG.
 */

#include <nexenne/random/discrete.hpp>
#include <nexenne/random/exponential.hpp>
#include <nexenne/random/gamma.hpp>
#include <nexenne/random/normal.hpp>
#include <nexenne/random/pcg.hpp>
#include <nexenne/random/poisson.hpp>
#include <nexenne/random/sample.hpp>
#include <nexenne/random/seed_seq.hpp>
#include <nexenne/random/uniform.hpp>
#include <nexenne/random/xoshiro.hpp>

namespace nexenne::random {}
