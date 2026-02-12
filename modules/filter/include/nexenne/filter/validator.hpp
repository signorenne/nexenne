#pragma once

/**
 * @file
 * @brief Generic predicate-based validation filter.
 */

#include <concepts>
#include <type_traits>
#include <utility>

namespace nexenne::filter {

/**
 * @brief Generic predicate-based validation filter.
 *
 * Accepts a sample only if a user-supplied predicate returns
 * \c true for it; otherwise the output holds the last accepted
 * value. This is the most flexible rejection filter: any
 * domain-specific validation logic that can be expressed as
 * \c bool(T const&) plugs in:
 *
 * \code
 * // Accept only even register values (bit 0 must be clear):
 * auto v{validator{[](std::uint16_t r){ return (r & 1u) == 0; }}};
 *
 * // Accept only readings where the status byte is OK:
 * auto v{validator{[](reading r){ return r.status == 0; }}};
 * \endcode
 *
 * If the very first sample fails the predicate, it is accepted
 * anyway (the filter needs /some/ initial state). Subsequent
 * failures hold the last-accepted value.
 *
 * The predicate is stored by value (like \c scope_timer's
 * callback) so a captured lambda inlines without
 * \c std::function overhead.
 *
 * @tparam T Sample type.
 * @tparam Pred Callable \c (T const&) -> bool.
 *
 * @note Reach for this when validity is an arbitrary domain rule (a
 * parity bit, a status byte, a plausibility check) expressed as a
 * predicate.
 */
template <typename T, std::predicate<T const&> Pred>
class validator {
public:
  using value_type = T;
  using predicate_type = Pred;

private:
  // A capturing lambda predicate need not be default-constructible, so
  // m_pred is initialised only through the constructors.
  predicate_type m_pred;
  value_type m_value{};
  bool m_primed{false};

public:
  /**
   * @brief Constructs an unprimed validator from a predicate.
   *
   * @param pred Callable \c (T const&) -> bool deciding whether a
   * sample is acceptable. Stored by value.
   *
   * @pre None.
   * @post The validator is unprimed, so its first \c push is accepted
   * regardless of the predicate result.
   */
  constexpr explicit validator(predicate_type pred
  ) noexcept(std::is_nothrow_move_constructible_v<predicate_type>)
      : m_pred{std::move(pred)} {}

  /**
   * @brief Constructs a validator primed to a known value.
   *
   * @param pred Acceptance predicate, stored by value.
   * @param initial Value held until the first accepted sample.
   *
   * @pre None.
   * @post \c value() returns \p initial; a first sample that fails the
   * predicate is rejected rather than seeded.
   */
  constexpr validator(
    predicate_type pred, value_type const initial
  ) noexcept(std::is_nothrow_move_constructible_v<predicate_type>)
      : m_pred{std::move(pred)}, m_value{initial}, m_primed{true} {}

  /**
   * @brief Feeds one sample, accepting it only if the predicate passes.
   *
   * An accepted sample becomes the new output. A rejected sample holds
   * the last accepted value, except that the very first sample of an
   * unprimed validator is accepted unconditionally so the filter has
   * an initial state.
   *
   * @param sample New input sample.
   *
   * @return The current accepted (held) value.
   *
   * @pre Invoking the predicate on \p sample does not throw (the
   * method is \c noexcept).
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1) plus the cost of one predicate call.
   */
  [[nodiscard]] constexpr auto push(value_type const sample) noexcept -> value_type {
    if (m_pred(sample) || !m_primed) {
      m_value = sample;
      m_primed = true;
    }
    return m_value;
  }

  /**
   * @brief Returns the current accepted value without advancing.
   *
   * @return The last value that passed the predicate (or the seeded
   * first sample).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return m_value;
  }

  /**
   * @brief Clears the validator back to the unprimed condition.
   *
   * @pre None.
   * @post \c value() returns a value-initialised \c T and the next
   * \c push is accepted unconditionally; the predicate is
   * retained.
   */
  constexpr auto reset() noexcept -> void {
    m_value = T{};
    m_primed = false;
  }
};

/**
 * @brief Deduction guide for constructing a validator from a predicate
 * and an initial value.
 *
 * Lets \c validator{pred, initial} deduce the sample type from the
 * second argument and the predicate type from the first.
 *
 * @tparam Pred Predicate type.
 * @tparam T Sample type.
 *
 * @pre None.
 * @post None.
 */
template <typename Pred, typename T>
validator(Pred, T) -> validator<T, Pred>;

}  // namespace nexenne::filter
