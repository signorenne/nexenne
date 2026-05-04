#pragma once

/**
 * @file
 * @brief Hash-combining primitives shared across nexenne modules.
 *
 * Centralises a seed-and-mix combiner in the style of \c boost::hash_combine
 * so every module that produces a composite \c std::hash draws from one recipe
 * and stays mutually consistent. The mix constants and shifts are width-tuned
 * at compile time for \c std::size_t, inspired by the Boost recipe but not
 * bit-compatible with any Boost release: a 32-bit target (common on the
 * embedded boards nexenne runs on) gets its own golden-ratio constant and
 * shift pair rather than a truncation of the 64-bit ones. Every combiner is
 * conditionally \c noexcept: it propagates whether the underlying \c std::hash
 * call can throw.
 */

#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>
#include <utility>

namespace nexenne::utility {

/**
 * @brief A type with a usable \c std::hash specialisation.
 *
 * @tparam T Candidate type.
 */
template <typename T>
concept hashable = requires(T const& value) {
  { std::hash<T>{}(value) } -> std::convertible_to<std::size_t>;
};

namespace detail {

/**
 * @brief Mixing constant and shift pair tuned for the width of \c std::size_t.
 *
 * The magic constant is the golden ratio scaled to \c 2^bits and the shifts
 * are picked per width, inspired by \c boost::hash_combine and Boost's
 * width-specialised \c hash_mix but matching no Boost variant bit for bit.
 * Only the 8-byte and 4-byte specialisations are defined.
 *
 * @tparam Width Size of \c std::size_t in bytes.
 *
 * @pre \p Width is 8 or 4.
 * @post None.
 */
template <std::size_t Width = sizeof(std::size_t)>
struct hash_mix {
  static_assert(
    Width == 4 || Width == 8, "nexenne::utility::hash supports only a 32-bit or 64-bit std::size_t"
  );
};

template <>
struct hash_mix<8> {
  static constexpr std::size_t magic{0x9e3779b97f4a7c15ULL};
  static constexpr int left{6};
  static constexpr int right{2};
};

template <>
struct hash_mix<4> {
  static constexpr std::size_t magic{0x9e3779b9U};
  static constexpr int left{15};
  static constexpr int right{13};
};

/**
 * @brief Whether hashing a \c T \c const& with \c std::hash cannot throw.
 *
 * Feeds the conditional \c noexcept of every combiner below, since a
 * user-provided \c std::hash specialisation is allowed to throw.
 *
 * @tparam T Hashable type to probe.
 */
template <typename T>
inline constexpr bool nothrow_hashable_v{noexcept(std::hash<T>{}(std::declval<T const&>()))};

}  // namespace detail

/**
 * @brief Mixes \p value into \p seed with a seed-and-mix combining step.
 *
 * Hashes \p value with \c std::hash and folds it into \p seed with the
 * width-tuned magic constant and shifts (a recipe in the style of
 * \c boost::hash_combine, not bit-compatible with it). The mixing is
 * order-sensitive. \c noexcept exactly when the \c std::hash call is.
 *
 * @tparam Value Hashable component type.
 * @param seed Accumulator, modified in place.
 * @param value Value to mix in.
 *
 * @pre None.
 * @post \p seed has been updated to incorporate \p value.
 */
template <hashable Value>
auto hash_combine(std::size_t& seed, Value const& value)
  noexcept(detail::nothrow_hashable_v<Value>) -> void {
  using mix = detail::hash_mix<>;
  auto const hashed{std::hash<Value>{}(value)};
  seed ^= hashed + mix::magic + (seed << mix::left) + (seed >> mix::right);
}

/**
 * @brief Combines an arbitrary number of values into an existing seed.
 *
 * Folds \c hash_combine over the pack left to right. An empty pack leaves the
 * seed unchanged. \c noexcept exactly when every \c std::hash call is.
 *
 * @tparam Args Hashable component types.
 * @param seed Accumulator, modified in place.
 * @param args Values to mix in, in order.
 *
 * @pre None.
 * @post \p seed has been updated to incorporate every value in \p args.
 */
template <hashable... Args>
auto hash_combine_each(std::size_t& seed, Args const&... args)
  noexcept((detail::nothrow_hashable_v<Args> && ...)) -> void {
  (hash_combine(seed, args), ...);
}

/**
 * @brief Hashes a pack of values into a fresh seed and returns it.
 *
 * Starts from a zero seed and folds every argument in with \c hash_combine, so
 * the result is order-sensitive. \c noexcept exactly when every \c std::hash
 * call is.
 *
 * @tparam Args Hashable component types.
 * @param args Values to hash, in order.
 *
 * @return The combined hash of \p args.
 *
 * @pre None.
 * @post None.
 *
 * @par Example
 * \code
 * auto const h{nexenne::utility::hash_args(point.x, point.y, point.z)};
 * \endcode
 */
template <hashable... Args>
[[nodiscard]] auto hash_args(Args const&... args)
  noexcept((detail::nothrow_hashable_v<Args> && ...)) -> std::size_t {
  auto seed{std::size_t{0}};
  hash_combine_each(seed, args...);
  return seed;
}

/**
 * @brief Hashes every element of a range into a fresh seed and returns it.
 *
 * Folds each element in with \c hash_combine in iteration order, so the result
 * is order-sensitive: \c [1,2] and \c [2,1] hash differently. An empty range
 * hashes to zero. \c noexcept exactly when the element \c std::hash call is.
 *
 * @tparam Range Range whose element type is hashable.
 * @param range Range whose elements are hashed in order.
 *
 * @return The combined hash of the range elements.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(M) for a range of \c M elements.
 */
template <std::ranges::input_range Range>
  requires hashable<std::ranges::range_value_t<Range>>
[[nodiscard]] auto hash_range(Range const& range)
  noexcept(detail::nothrow_hashable_v<std::ranges::range_value_t<Range>>) -> std::size_t {
  auto seed{std::size_t{0}};
  for (auto const& element : range) {
    hash_combine(seed, element);
  }
  return seed;
}

}  // namespace nexenne::utility
