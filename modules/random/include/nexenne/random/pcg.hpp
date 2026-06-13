#pragma once

/**
 * @file
 * @brief PCG32, a small, fast, high-quality non-cryptographic
 *        PRNG by Melissa O'Neill.
 *
 * 64-bit state, 32-bit output. Passes every statistical test
 * relevant to non-crypto use (TestU01 BigCrush) at a fraction
 * of the size and runtime cost of \c std::mt19937. Default
 * choice when you need an RNG and don't have a specific reason
 * to pick another.
 *
 * Output: a single \c std::uint32_t per \c next() call. To
 * extract bounded integers or \c [0,1) doubles, see
 * \c nexennerandomuniform.hpp.
 *
 * This implementation models the engine concept just enough to
 * cooperate with the C++ \c \<random\> distributions when you want
 * them: it has \c result_type, \c min, \c max, and an
 * \c operator()(). It does not inherit from anything and does
 * not require \c \<random\>.
 *
 * Reproducibility: stream + state fully determine the sequence.
 * Two engines constructed with the same \c (state, sequence)
 * produce identical output.
 *
 * \warning Not cryptographically secure. If you need unguessable
 *          bits, use a cipher-based or OS RNG, not this.
 */

#include <bit>
#include <concepts>
#include <cstdint>
#include <limits>

namespace nexenne::random {

class pcg32 {
public:
  using result_type = std::uint32_t;

  static constexpr auto default_state{std::uint64_t{0x853c'49e6'748f'ea9bULL}};
  static constexpr auto default_sequence{std::uint64_t{0xda3e'39cb'94b9'5bdbULL}};

private:
  std::uint64_t m_state{0};
  std::uint64_t m_inc{1};

public:
  /**
   * @brief Constructs a PCG32 seeded with the default state and stream.
   *
   * Delegates to the two-argument constructor with the library's
   * fixed default constants, giving a reproducible default sequence.
   *
   * @pre None.
   * @post The engine is ready to produce its default output sequence.
   */
  constexpr pcg32() noexcept : pcg32{default_state, default_sequence} {}

  /**
   * @brief Constructs a PCG32 from a state and stream-selection value.
   *
   * The \p sequence value selects the output stream: two engines with
   * the same \p state but different \p sequence values produce
   * independent sequences.
   *
   * @param state Initial state that seeds the generator.
   * @param sequence Stream-selection constant; any value is valid and
   *                 is folded into an odd increment internally.
   *
   * @pre None. Every \p state and \p sequence pair is accepted.
   * @post The engine produces a sequence fully determined by
   *       \p state and \p sequence.
   */
  constexpr pcg32(std::uint64_t const state, std::uint64_t const sequence) noexcept
      : m_state{0}, m_inc{(sequence << 1u) | 1u} {
    static_cast<void>(next());
    m_state += state;
    static_cast<void>(next());
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
   * @brief Produces the next 32-bit output and advances the state.
   *
   * @return A pseudo-random value uniformly distributed over the full
   *         \c result_type range.
   *
   * @pre None.
   * @post The internal state has advanced by one step.
   *
   * @complexity \c O(1).
   */
  constexpr auto next() noexcept -> result_type {
    auto const old{m_state};
    m_state = old * std::uint64_t{6364136223846793005ULL} + m_inc;
    auto const xorshifted{static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u)};
    auto const rot{static_cast<std::uint32_t>(old >> 59u)};
    return std::rotr(xorshifted, static_cast<int>(rot));
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
   * @brief Jumps the engine \p delta steps along its sequence.
   *
   * Uses the LCG jump-ahead formula, so the cost is logarithmic in
   * the magnitude of \p delta. A negative \p delta runs the sequence
   * backward by wrapping into the unsigned step count.
   *
   * @param delta Number of steps to advance; negative values move
   *              backward.
   *
   * @pre None.
   * @post The state equals what \c |delta| forward (or backward) calls
   *       to \c next would have produced.
   *
   * @complexity \c O(log |delta|).
   */
  constexpr auto advance(std::int64_t const delta) noexcept -> void {
    auto cur_mult{std::uint64_t{6364136223846793005ULL}};
    auto cur_plus{m_inc};
    auto acc_mult{std::uint64_t{1}};
    auto acc_plus{std::uint64_t{0}};
    auto remaining{static_cast<std::uint64_t>(delta)};
    while (remaining > 0) {
      if ((remaining & 1u) != 0) {
        acc_mult *= cur_mult;
        acc_plus = acc_plus * cur_mult + cur_plus;
      }
      cur_plus = (cur_mult + 1) * cur_plus;
      cur_mult *= cur_mult;
      remaining >>= 1u;
    }
    m_state = acc_mult * m_state + acc_plus;
  }

  /**
   * @brief Returns the current internal state.
   *
   * @return The 64-bit state word. Together with the fixed stream this
   *         determines all future output.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto state() const noexcept -> std::uint64_t {
    return m_state;
  }
};

}  // namespace nexenne::random
