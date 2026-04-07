#pragma once

/**
 * @file
 * @brief xoshiro256** - a 256-bit state, 64-bit output PRNG by
 *        Blackman and Vigna.
 *
 * Faster than PCG64 on most pipelines, with a longer period
 * (2^256, 1) and excellent statistical quality. Use this when:
 *
 *   - You want a 64-bit-native output (\c pcg32 is 32-bit).
 *   - You're sampling huge numbers of values where the
 *     per-call cycle savings matter (Monte Carlo, particle
 *     systems, large procedural worlds).
 *   - You need cheap parallel streams via \c jump() (2^128
 *     non-overlapping subsequences) or \c long_jump() (2^192).
 *
 * Seeding: zero is not a valid state. Construct with a non-zero
 * seed; the constructor splits one 64-bit seed across the four
 * lanes via SplitMix64 so any non-zero input gives a usable state.
 *
 * \warning Not cryptographically secure.
 */

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <limits>

#include <nexenne/utility/discard.hpp>

namespace nexenne::random {

class xoshiro256ss {
public:
  using result_type = std::uint64_t;

private:
  static constexpr auto splitmix64(std::uint64_t& s) noexcept -> std::uint64_t {
    s += std::uint64_t{0x9e37'79b9'7f4a'7c15ULL};
    auto z{s};
    z = (z ^ (z >> 30u)) * std::uint64_t{0xbf58'476d'1ce4'e5b9ULL};
    z = (z ^ (z >> 27u)) * std::uint64_t{0x94d0'49bb'1331'11ebULL};
    return z ^ (z >> 31u);
  }

  constexpr auto apply_jump(std::array<std::uint64_t, 4> const& j) noexcept -> void {
    auto s{std::array<std::uint64_t, 4>{0, 0, 0, 0}};
    for (auto const word : j) {
      for (auto b{0u}; b < 64u; ++b) {
        if ((word & (std::uint64_t{1} << b)) != 0) {
          s[0] ^= m_s[0];
          s[1] ^= m_s[1];
          s[2] ^= m_s[2];
          s[3] ^= m_s[3];
        }
        nexenne::utility::discard(next());
      }
    }
    m_s = s;
  }

  std::array<std::uint64_t, 4> m_s{};

public:
  /**
   * @brief Constructs an engine seeded with the golden-ratio constant.
   *
   * Delegates to the seeding constructor with a fixed non-zero value,
   * giving a reproducible default sequence.
   *
   * @pre None.
   * @post The engine holds a valid non-zero state.
   */
  constexpr xoshiro256ss() noexcept : xoshiro256ss{0x9e37'79b9'7f4a'7c15ULL} {}

  /**
   * @brief Seeds all four state lanes from a single 64-bit value.
   *
   * Expands \p seed across the four lanes with SplitMix64. A zero
   * \p seed is replaced with the golden-ratio constant so the engine
   * never enters the forbidden all-zero state.
   *
   * @param seed Seed value; any value, including zero, is accepted.
   *
   * @pre None. A zero \p seed is substituted internally.
   * @post The state is non-zero and the engine is ready to use.
   */
  constexpr explicit xoshiro256ss(std::uint64_t seed) noexcept {
    if (seed == 0) {
      seed = 0x9e37'79b9'7f4a'7c15ULL;
    }
    for (auto& lane : m_s) {
      lane = splitmix64(seed);
    }
  }

  /**
   * @brief Returns the smallest value \c next can produce.
   *
   * @return Zero, the minimum of \c result_type.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto min() noexcept -> result_type {
    return std::numeric_limits<result_type>::min();
  }

  /**
   * @brief Returns the largest value \c next can produce.
   *
   * @return The maximum of \c result_type.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max() noexcept -> result_type {
    return std::numeric_limits<result_type>::max();
  }

  /**
   * @brief Produces the next 64-bit output and advances the state.
   *
   * @return A pseudo-random value uniformly distributed over the full
   *         \c result_type range.
   *
   * @pre The state is not all zero (guaranteed by every constructor).
   * @post The internal state has advanced by one step.
   *
   * @complexity \c O(1).
   */
  constexpr auto next() noexcept -> result_type {
    auto const result{std::rotl(m_s[1] * 5, 7) * 9};
    auto const t{m_s[1] << 17u};

    m_s[2] ^= m_s[0];
    m_s[3] ^= m_s[1];
    m_s[1] ^= m_s[2];
    m_s[0] ^= m_s[3];

    m_s[2] ^= t;
    m_s[3] = std::rotl(m_s[3], 45);

    return result;
  }

  /**
   * @brief Generates the next output, for standard-library interop.
   *
   * Equivalent to \c next; provided so the engine models
   * \c std::uniform_random_bit_generator.
   *
   * @return The next pseudo-random value.
   *
   * @pre None.
   * @post The internal state has advanced by one step.
   *
   * @complexity \c O(1).
   */
  constexpr auto operator()() noexcept -> result_type {
    return next();
  }

  /**
   * @brief Advances the engine by 2^128 calls in constant time.
   *
   * Equivalent to calling \c next 2^128 times. Use it to create
   * non-overlapping parallel streams: copy the engine, run \c jump
   * once per copy, and hand each copy to a separate task.
   *
   * @pre None.
   * @post The state equals what 2^128 calls to \c next would have
   *       produced.
   *
   * @complexity \c O(1).
   */
  constexpr auto jump() noexcept -> void {
    constexpr auto j{std::array<std::uint64_t, 4>{
      0x180e'c6d3'3cfd'0abaULL,
      0xd5a6'1266'f0c9'392cULL,
      0xa958'2618'e03f'c9aaULL,
      0x39ab'dc45'29b1'661cULL
    }};
    apply_jump(j);
  }

  /**
   * @brief Advances the engine by 2^192 calls in constant time.
   *
   * Equivalent to calling \c next 2^192 times. Use it to partition the
   * sequence into up to 2^64 streams, each of which can be further
   * subdivided with \c jump.
   *
   * @pre None.
   * @post The state equals what 2^192 calls to \c next would have
   *       produced.
   *
   * @complexity \c O(1).
   */
  constexpr auto long_jump() noexcept -> void {
    constexpr auto j{std::array<std::uint64_t, 4>{
      0x76e1'5d3e'fefd'cbbfULL,
      0xc500'4e44'1c52'2fb3ULL,
      0x7771'0069'854e'e241ULL,
      0x3910'9bb0'2acb'e635ULL
    }};
    apply_jump(j);
  }

  /**
   * @brief Returns the current internal state.
   *
   * @return A copy of the four 64-bit state lanes, which fully
   *         determine all future output.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto state() const noexcept -> std::array<std::uint64_t, 4> {
    return m_s;
  }
};

}  // namespace nexenne::random
