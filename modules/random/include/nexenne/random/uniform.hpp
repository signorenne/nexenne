#pragma once

/**
 * @file
 * @brief Bounded-range samplers that are faster and more
 *        portable than \c std::uniform_int_distribution and
 *        \c std::uniform_real_distribution.
 *
 * The standard distributions are notoriously non-portable: the
 * same engine + the same distribution can produce different
 * sequences under libstdc++ vs. libc++ vs. MSVC. For
 * reproducibility (replay logs, deterministic tests, networked
 * simulation), pin the algorithm yourself, that's what this
 * header is for.
 *
 * Integer uniform: Lemire's nearly-divisionless method
 * (https://arxiv.orgabs1805.10941). On modern CPUs this saves
 * a 64-bit divide on most iterations and matches the spec exactly.
 *
 * Real uniform: extract 53 random bits and divide by 2^53. The
 * resulting double is uniformly distributed in \c [0, 1) and is
 * the largest-period representation that respects IEEE-754
 * rounding.
 *
 * Engine concept: any callable that returns a 32-bit or 64-bit
 * unsigned integer satisfies these helpers. PCG32 and
 * xoshiro256** both qualify out of the box.
 */

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace nexenne::random {

/**
 * @brief Constrains a type to the minimal engine interface used here.
 *
 * A \c rng_engine exposes an unsigned-integral \c result_type and a
 * \c next() member returning that type. Both \c pcg32 and
 * \c xoshiro256ss satisfy it out of the box.
 *
 * @tparam G Candidate engine type.
 *
 * @pre None.
 * @post None.
 */
template <typename G>
concept rng_engine = requires(G g) {
  typename G::result_type;
  requires std::unsigned_integral<typename G::result_type>;
  // The samplers below assume a 32-bit or 64-bit word per draw (a 53-bit
  // mantissa needs at least 32 bits, assembled in 32/64-bit chunks). A
  // narrower result_type would silently confine the output range, so it is
  // rejected here rather than producing broken draws.
  requires sizeof(typename G::result_type) == 4 || sizeof(typename G::result_type) == 8;
  { g.next() } -> std::same_as<typename G::result_type>;
};

/**
 * @brief Draws a uniform integer in the closed range \c [lo, hi].
 *
 * Uses Lemire's nearly-divisionless rejection sampler. Ranges that fit
 * in 32 bits take a single 32-bit engine slice; wider ranges fall back
 * to modulo-with-rejection over a 64-bit slice. Both branches have
 * exactly zero bias.
 *
 * @tparam Int Integer result type.
 * @tparam G Engine type satisfying \c rng_engine.
 * @param g Engine to draw bits from; advanced by at least one step.
 * @param lo Inclusive lower bound.
 * @param hi Inclusive upper bound.
 *
 * @return A uniformly distributed integer in \c [lo, hi].
 *
 * @pre \p lo is less than or equal to \p hi.
 * @post The returned value lies in \c [lo, hi]; \p g has advanced by one
 *       or more steps.
 *
 * @complexity Expected \c O(1); worst case unbounded due to rejection,
 *             but the expected number of rejections is below one.
 */
template <std::integral Int, rng_engine G>
[[nodiscard]] constexpr auto uniform_int(G& g, Int const lo, Int const hi) noexcept -> Int {
  using U = std::make_unsigned_t<Int>;
  auto const lo_u{static_cast<U>(lo)};
  auto const range{static_cast<U>(static_cast<U>(hi) - lo_u + U{1})};

  if (range == 0) {
    // hi == max && lo == min: full-range, no rejection needed. Fill the full
    // width of U: a 32-bit engine must be drawn twice to cover a 64-bit Int,
    // otherwise the high word would be stuck at zero.
    using engine_u = typename G::result_type;
    auto bits{static_cast<U>(g.next())};
    if constexpr (sizeof(U) > sizeof(engine_u)) {
      for (auto filled{sizeof(engine_u)}; filled < sizeof(U); filled += sizeof(engine_u)) {
        bits = static_cast<U>(
          static_cast<U>(bits << (sizeof(engine_u) * 8u)) | static_cast<U>(g.next())
        );
      }
    }
    return static_cast<Int>(bits);
  }

  if (range <= U{0xFFFF'FFFFu}) {
    // Narrow path: Lemire's 32-bit form with a 64-bit widening multiply.
    auto const r32{static_cast<std::uint32_t>(range)};
    while (true) {
      auto const x{static_cast<std::uint32_t>(g.next())};
      auto const m{static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(r32)};
      auto const low{static_cast<std::uint32_t>(m)};
      if (low >= r32) {
        return static_cast<Int>(lo_u + static_cast<U>(m >> 32u));
      }
      auto const threshold{(0u - r32) % r32};
      if (low >= threshold) {
        return static_cast<Int>(lo_u + static_cast<U>(m >> 32u));
      }
    }
  }

  // Wide path: range > 2^32. Modulo-with-rejection on 64-bit slices.
  using engine_u = typename G::result_type;
  auto const r64{static_cast<std::uint64_t>(range)};
  auto const threshold{(std::uint64_t{0} - r64) % r64};
  while (true) {
    auto x{static_cast<std::uint64_t>(g.next())};
    if constexpr (sizeof(engine_u) < 8) {
      x = (x << 32u) | static_cast<std::uint64_t>(g.next());
    }
    if (x >= threshold) {
      return static_cast<Int>(lo_u + static_cast<U>(x % r64));
    }
  }
}

/**
 * @brief Draws a uniform real in \c [0, 1).
 *
 * Takes the top 53 bits of one 64-bit engine call (or two calls for a
 * 32-bit engine) and divides by \c 2^53, giving the largest-period
 * representation consistent with IEEE-754 rounding.
 *
 * @tparam G Engine type satisfying \c rng_engine.
 * @param g Engine to draw bits from; advanced by one or two steps.
 *
 * @return A uniformly distributed \c double in \c [0, 1).
 *
 * @pre None.
 * @post The result lies in \c [0, 1); \p g has advanced by one or two
 *       steps.
 *
 * @complexity \c O(1).
 */
template <rng_engine G>
[[nodiscard]] constexpr auto uniform_real(G& g) noexcept -> double {
  using engine_u = typename G::result_type;
  auto bits{std::uint64_t{0}};
  if constexpr (sizeof(engine_u) >= 8) {
    bits = static_cast<std::uint64_t>(g.next());
  } else {
    auto const a{static_cast<std::uint64_t>(g.next())};
    auto const b{static_cast<std::uint64_t>(g.next())};
    bits = (a << 32u) | b;
  }
  constexpr auto k{53u};
  return static_cast<double>(bits >> (64u - k))
         * (1.0 / static_cast<double>(std::uint64_t{1} << k));
}

/**
 * @brief Flips a fair coin.
 *
 * Returns the low bit of one engine output, which is equivalent to
 * \c uniform_int(g, 0, 1) == 1 but skips the rejection sampler and
 * consumes a single engine call.
 *
 * @tparam G Engine type satisfying \c rng_engine.
 * @param g Engine to draw from; advanced by one step.
 *
 * @return \c true and \c false with equal probability.
 *
 * @pre None.
 * @post \p g has advanced by one step.
 *
 * @complexity \c O(1).
 */
template <rng_engine G>
[[nodiscard]] constexpr auto bernoulli(G& g) noexcept -> bool {
  return (g.next() & 1u) != 0u;
}

/**
 * @brief Draws a Bernoulli trial with success probability \p p.
 *
 * Returns \c true with probability \p p. Out-of-range probabilities are
 * short-circuited: \p p at or below zero always yields \c false and
 * \p p at or above one always yields \c true.
 *
 * @tparam G Engine type satisfying \c rng_engine.
 * @param g Engine to draw from; advanced by zero steps for the
 *           clamped cases, otherwise by the cost of one \c uniform_real.
 * @param p Success probability.
 *
 * @return \c true with probability \c clamp(p, 0, 1).
 *
 * @pre None. \p p is effectively clamped to \c [0, 1].
 * @post None.
 *
 * @complexity \c O(1).
 */
template <rng_engine G>
[[nodiscard]] constexpr auto bernoulli(G& g, double const p) noexcept -> bool {
  if (p <= 0.0) {
    return false;
  }
  if (p >= 1.0) {
    return true;
  }
  return uniform_real(g) < p;
}

}  // namespace nexenne::random
