#pragma once

/**
 * @file
 * @brief Probabilistic set membership with a controlled false-positive rate.
 *
 * \c bloom_filter<T, Hash> packs membership into a runtime-sized bit array.
 * \c insert sets \c k bits derived from \p T's hash; \c contains checks them: if
 * any is unset the value is *definitely absent*, if all are set it is *probably
 * present* (a false positive at the configured rate). It never reports a false
 * negative. The trade against an exact set is dramatic: roughly ten bits per
 * element for a one-percent false-positive rate, with no value storage and no
 * enumeration, so it shines as a cheap pre-filter that rejects most misses before
 * an expensive exact check (a disk seek, a network round-trip, a breach-list
 * lookup).
 *
 * Sizing for \c n items at false-positive rate \c p uses
 * \c m = ceil(-n ln p / (ln 2)^2) bits and \c k = ceil((m/n) ln 2) hashes; the
 * \c with_target_false_positive_rate factory does that arithmetic. Internally one
 * \c std::hash evaluation is stretched into \c k positions by double hashing
 * (\c h1 + i*h2, the Kirsch-Mitzenmacher construction), avoiding \c k separate
 * hash functors. It has no \c erase (removing a bit could create a false negative
 * for another element) and cannot enumerate its contents. Every operation is
 * \c noexcept; allocation failure terminates.
 */

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <utility>

#include <nexenne/container/bitset_dynamic.hpp>
#include <nexenne/container/error.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::container {

/**
 * @brief Probabilistic membership filter with no false negatives.
 *
 * @tparam T Hashable value type.
 * @tparam Hash Hash functor; \c std::hash<T> by default.
 *
 * @pre None.
 * @post A constructed filter is empty over its configured bit and hash counts.
 */
template <typename T, typename Hash = std::hash<T>>
class bloom_filter {
public:
  using value_type = T;
  using size_type = std::size_t;
  using hasher = Hash;

private:
  bitset_dynamic m_bits;
  size_type m_num_hashes{};
  size_type m_insertions{};
  Hash m_hash{};

  // Second hash, derived from the first: "Less Hashing, Same Performance"
  // (Kirsch and Mitzenmacher) shows h_i(x) = h1 + i*h2 matches k independent
  // hashes for the error rate.
  [[nodiscard]] static constexpr auto splitmix64(std::uint64_t z) noexcept -> std::uint64_t {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z = z ^ (z >> 31);
    return z;
  }

  // The two base hashes for a value. h2 is forced odd so it is never zero:
  // a value whose hash is 0 would otherwise drive every one of the k positions
  // to bit 0 (splitmix64(0) == 0), collapsing the filter for that value.
  [[nodiscard]] auto hash_pair(T const& value
  ) const noexcept -> std::pair<std::size_t, std::size_t> {
    auto const h1{m_hash(value)};
    auto const h2{static_cast<std::size_t>(splitmix64(h1)) | std::size_t{1}};
    return {h1, h2};
  }

  [[nodiscard]] auto bit_for(size_type const k, std::size_t const h1, std::size_t const h2)
    const noexcept -> size_type {
    return (h1 + k * h2) % m_bits.size();
  }

public:
  /**
   * @brief Constructs a filter with an explicit bit-array size and hash count.
   *
   * Prefer \c with_target_false_positive_rate for the common "size for N items at
   * rate P" case.
   *
   * @param bits Number of bits in the backing array.
   * @param k Number of hash positions set per insertion.
   *
   * @pre \p bits and \p k are both greater than zero (asserted in debug); a zero
   *      bit count would divide by zero when mapping a hash to a bit.
   * @post \c bit_count() equals \p bits, \c hash_count() equals \p k, and
   *       \c empty() is \c true.
   */
  bloom_filter(size_type const bits, size_type const k) noexcept : m_bits(bits), m_num_hashes{k} {
    assert(bits > 0 && k > 0 && "bloom_filter requires a positive bit and hash count");
  }

  /**
   * @brief Factory: a filter sized for \p expected_items at rate \p target_fpr.
   *
   * @param expected_items Number of elements the filter is sized for.
   * @param target_fpr Desired false-positive probability in (0, 1).
   *
   * @return A filter whose bit and hash counts satisfy the sizing recipe.
   *
   * @pre \p expected_items is greater than zero and \p target_fpr is in (0, 1).
   * @post The returned filter is empty with \c hash_count() at least one.
   */
  [[nodiscard]] static auto with_target_false_positive_rate(
    size_type const expected_items, double const target_fpr
  ) noexcept -> bloom_filter {
    auto const ln2{0.6931471805599453};
    auto const m{static_cast<size_type>(
      std::ceil(-static_cast<double>(expected_items) * std::log(target_fpr) / (ln2 * ln2))
    )};
    auto const k{std::max<size_type>(
      1U,
      static_cast<size_type>(
        std::ceil((static_cast<double>(m) / static_cast<double>(expected_items)) * ln2)
      )
    )};
    return bloom_filter{m, k};
  }

  /**
   * @brief Number of bits in the backing array.
   *
   * @return The configured bit count.
   *
   * @pre None.
   * @post None. The filter is not modified.
   */
  [[nodiscard]] constexpr auto bit_count() const noexcept -> size_type {
    return m_bits.size();
  }

  /**
   * @brief Number of hash positions set per insertion.
   *
   * @return The configured hash count \c k.
   *
   * @pre None.
   * @post None. The filter is not modified.
   */
  [[nodiscard]] constexpr auto hash_count() const noexcept -> size_type {
    return m_num_hashes;
  }

  /**
   * @brief Count of \c insert calls since construction or the last clear.
   *
   * @return Total insertions recorded, including repeated values.
   *
   * @pre None.
   * @post None. The filter is not modified.
   */
  [[nodiscard]] constexpr auto insertions() const noexcept -> size_type {
    return m_insertions;
  }

  /**
   * @brief Whether nothing has been inserted.
   *
   * @return \c true when \c insertions() is zero.
   *
   * @pre None.
   * @post None. The filter is not modified.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return m_insertions == 0;
  }

  /**
   * @brief Theoretical false-positive probability at the current load.
   *
   * Assumes uniform hashing.
   *
   * @return Estimated false-positive rate in [0, 1].
   *
   * @pre None.
   * @post None. The filter is not modified.
   */
  [[nodiscard]] auto false_positive_rate() const noexcept -> double {
    if (m_insertions == 0) {
      return 0.0;
    }
    auto const m{static_cast<double>(m_bits.size())};
    auto const n{static_cast<double>(m_insertions)};
    auto const k{static_cast<double>(m_num_hashes)};
    return std::pow(1.0 - std::exp(-k * n / m), k);
  }

  /**
   * @brief Clears every bit and resets the insertion counter.
   *
   * @pre None.
   * @post \c empty() is \c true and \c contains returns \c false for every value;
   *       bit and hash counts are unchanged.
   */
  auto clear() noexcept -> void {
    m_bits.reset_all();
    m_insertions = 0;
  }

  /**
   * @brief Swaps contents with \p other.
   *
   * @param other Filter to exchange state with.
   *
   * @pre None.
   * @post This filter holds \p other's former state and vice versa.
   */
  auto swap(bloom_filter& other) noexcept -> void {
    using std::swap;
    m_bits.swap(other.m_bits);
    swap(m_num_hashes, other.m_num_hashes);
    swap(m_insertions, other.m_insertions);
    swap(m_hash, other.m_hash);
  }

  /**
   * @brief Swaps the contents of \p a and \p b.
   *
   * @param a First filter.
   * @param b Second filter.
   *
   * @pre None.
   * @post \p a and \p b have exchanged state.
   */
  friend auto swap(bloom_filter& a, bloom_filter& b) noexcept -> void {
    a.swap(b);
  }

  /**
   * @brief Records \p value in the filter.
   *
   * Idempotent on the bit array for repeats (already-set bits stay set), but the
   * insertion counter still increments.
   *
   * @param value Value to record.
   *
   * @pre None.
   * @post \c contains(value) returns \c true and \c insertions() grew by one.
   *
   * @complexity \c O(k) hash positions.
   */
  auto insert(T const& value) noexcept -> void {
    auto const [h1, h2]{hash_pair(value)};
    for (size_type k{0}; k < m_num_hashes; ++k) {
      nexenne::utility::discard(m_bits.set(bit_for(k, h1, h2)));
    }
    ++m_insertions;
  }

  /**
   * @brief Whether \p value might have been inserted.
   *
   * @param value Value to test for possible membership.
   *
   * @return \c false when \p value was definitely never inserted, \c true
   *         otherwise (possibly a false positive at the configured rate).
   *
   * @pre None.
   * @post None. The filter is not modified.
   *
   * @complexity \c O(k) hash positions.
   */
  [[nodiscard]] auto contains(T const& value) const noexcept -> bool {
    auto const [h1, h2]{hash_pair(value)};
    for (size_type k{0}; k < m_num_hashes; ++k) {
      if (!m_bits[bit_for(k, h1, h2)]) {
        return false;
      }
    }
    return true;
  }

  /**
   * @brief Unions \p other into this filter.
   *
   * Afterwards \c contains returns \c true for any value previously in either
   * filter. Both must share \c bit_count and \c hash_count.
   *
   * @param other Filter to union into this one.
   *
   * @return Nothing on success, or \c container_error::out_of_range when the
   *         filters differ in \c bit_count or \c hash_count.
   *
   * @pre None. The shapes are checked.
   * @post On success this filter is the bitwise union of both; on failure it is
   *       unchanged.
   *
   * @complexity \c O(bit_count).
   */
  auto merge(bloom_filter const& other) noexcept -> std::expected<void, container_error> {
    if (m_bits.size() != other.m_bits.size() || m_num_hashes != other.m_num_hashes) {
      return std::unexpected{container_error::out_of_range};
    }
    m_bits |= other.m_bits;
    m_insertions += other.m_insertions;
    return {};
  }

  /**
   * @brief Whether \p a and \p b have identical bits and hash count.
   *
   * @param a First filter.
   * @param b Second filter.
   *
   * @return \c true when both share the same \c hash_count and bit array.
   *
   * @pre None.
   * @post None. Neither filter is modified.
   */
  [[nodiscard]] friend auto
  operator==(bloom_filter const& a, bloom_filter const& b) noexcept -> bool {
    return a.m_num_hashes == b.m_num_hashes && a.m_bits == b.m_bits;
  }
};

}  // namespace nexenne::container
