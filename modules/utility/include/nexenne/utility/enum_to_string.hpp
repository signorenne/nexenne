#pragma once

/**
 * @file
 * @brief Compile-time enum reflection via \c __PRETTY_FUNCTION__ parsing.
 *
 * Forward (value to name) and inverse (name to value) reflection:
 * \c enum_name<V>() (compile-time name of a literal enumerator),
 * \c enum_to_string(value) (runtime value to name), \c enum_count<E>() and
 * \c enum_values<E>() (compile-time census of named enumerators), and
 * \c enum_cast<E>(name) (runtime name to value). The runtime and census entry
 * points scan a bounded window of underlying values, defaulting to
 * \c [0, 256); widen or shift it with the \c Range and \c Min parameters for
 * enums (including signed ones) whose enumerators fall outside that window.
 * The window is clamped to the values representable by the enum's underlying
 * type, so an oversized \c Range never wraps a narrow underlying type and
 * never scans a value twice. The scanned enum must have a fixed underlying
 * type (every scoped enum does; an unscoped enum needs an explicit enum-base):
 * without one, casting a scanned value outside the enum's range of values is
 * undefined behaviour, and Clang rejects it with a hard error during constant
 * evaluation. Requires GCC or Clang; other compilers see no named enumerators.
 */

#include <array>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief An enumeration type (scoped or unscoped).
 *
 * @tparam E Candidate type.
 */
template <typename E>
concept enumeration = std::is_enum_v<E>;

namespace detail {

/// @cond INTERNAL

/**
 * @brief Extracts the enumerator name of \p V from \c __PRETTY_FUNCTION__.
 *
 * Parses the compiler's pretty-function signature for the token after
 * \c "V = ", strips any qualifier prefix, and rejects the signature
 * placeholders a non-enumerator value produces (a residual parenthesis, or a
 * leading digit or minus). Returns an empty view on an unsupported compiler.
 *
 * @tparam V A literal enumerator value of some enum type.
 *
 * @return The enumerator's name, or an empty view when \p V is not a named
 *         enumerator or the compiler is unsupported.
 *
 * @pre None.
 * @post None.
 */
template <auto V>
[[nodiscard]] constexpr auto enum_value_name() noexcept -> std::string_view {
#if defined(__GNUC__) || defined(__clang__)
  auto const fn{std::string_view{__PRETTY_FUNCTION__}};
  // GCC: "... enum_value_name() [with auto V = E::name; ...]"
  // Clang: "auto detail::enum_value_name() [V = E::name]"; both share "V = ".
  auto const eq{fn.find("V = ")};
  if (eq == std::string_view::npos) {
    return {};
  }
  auto const start{eq + 4};
  auto const term{fn.find_first_of(",;]", start)};
  if (term == std::string_view::npos) {
    return {};
  }
  auto name{fn.substr(start, term - start)};
  // Strip any qualifier prefix to leave the trailing token. The prefix can
  // contain parentheses (an enumerator in an anonymous namespace renders as
  // "(anonymous namespace)::E::name"), so it MUST be stripped before the
  // placeholder check below, or valid enumerators are wrongly rejected.
  auto const sep{name.rfind("::")};
  if (sep != std::string_view::npos) {
    name.remove_prefix(sep + 2);
  }
  // Reject signature placeholders for non-enumerator values: compilers render
  // those as "(E)0" or "((anonymous namespace)::E)0", so after stripping the
  // token still carries a parenthesis or starts with a digit or '-'.
  if (name.empty() || name.find_first_of("()") != std::string_view::npos) {
    return {};
  }
  auto const first{name.front()};
  if (first == '-' || (first >= '0' && first <= '9')) {
    return {};
  }
  return name;
#else
  return {};
#endif
}

/**
 * @brief Reports whether \p V is a named enumerator whose name equals \p name.
 *
 * @tparam V A literal enumerator value of some enum type.
 * @param name Candidate enumerator name to compare against.
 *
 * @return \c true when \p V names a real enumerator whose name equals \p name.
 *
 * @pre None.
 * @post None.
 */
template <auto V>
[[nodiscard]] constexpr auto enum_name_matches(std::string_view const name) noexcept -> bool {
  auto const candidate{enum_value_name<V>()};
  return !candidate.empty() && candidate == name;
}

/**
 * @brief A half-open scan window \c [min, min + range) of underlying values.
 *
 * Produced by \c clamped_window and consumed by every range-scanning entry
 * point.
 *
 * @pre None.
 * @post None.
 */
struct scan_window {
  int min;    ///< First underlying value in the window.
  int range;  ///< Number of underlying values in the window.
};

/**
 * @brief Clamps a requested scan window to \p E's representable underlying values.
 *
 * When the underlying type is at least as wide as \c int the int-to-underlying
 * conversion is injective over the whole window, so no clamping is needed. For
 * a narrower type the excess values would wrap (\c static_cast to a narrow type
 * of 256 is 0) and re-visit underlying values already scanned, double-counting
 * enumerators and breaking the ascending order of \c enum_values, so the window
 * is cut to the representable range instead.
 *
 * @tparam E Enum type whose underlying value range bounds the window.
 * @param min First requested underlying value to scan.
 * @param range Requested number of underlying values to scan.
 *
 * @return The window clamped to \p E's representable underlying values.
 *
 * @pre None.
 * @post The returned window lies within \p E's underlying value range.
 */
template <typename E>
[[nodiscard]] consteval auto clamped_window(int const min, int const range) noexcept
  -> scan_window {
  using underlying = std::underlying_type_t<E>;
  if constexpr (sizeof(underlying) >= sizeof(int)) {
    return {min, range};
  } else {
    auto const type_lo{static_cast<int>(std::numeric_limits<underlying>::min())};
    auto const type_hi{static_cast<int>(std::numeric_limits<underlying>::max())};
    auto const lo{min < type_lo ? type_lo : min};
    // 64-bit arithmetic: min + range can overflow int when both are large.
    auto const requested_end{static_cast<long long>(min) + range};
    auto const end{
      requested_end > type_hi + 1LL ? type_hi + 1 : static_cast<int>(requested_end)
    };
    return {lo, end > lo ? end - lo : 0};
  }
}

// The integer sequences below are built over int, not the underlying type, to
// avoid overflow when the underlying type is narrow: a
// std::make_integer_sequence<std::uint8_t, 256> would silently produce zero
// elements because 256 is not representable.

/**
 * @brief Finds the name of the enumerator whose underlying value is \p target.
 *
 * Expands the index pack into a flat braced-init sequence (not a fold, which
 * would exceed Clang's expression-nesting limit) and returns the first matching
 * enumerator name, scanning underlying values from \p Min upward.
 *
 * @tparam E Enum type being searched.
 * @tparam Min First underlying value the index pack maps to.
 * @tparam Is Index pack offsetting \p Min across the scan window.
 * @param target Underlying value to match.
 *
 * @return The matching enumerator name, or an empty view when none matches.
 *
 * @pre None.
 * @post None.
 */
template <typename E, int Min, int... Is>
[[nodiscard]] constexpr auto
enum_search(std::underlying_type_t<E> const target, std::integer_sequence<int, Is...>) noexcept
  -> std::string_view {
  auto result{std::string_view{}};
  // A braced-init-list expands the pack into a flat, left-to-right sequence.
  // A fold expression would nest one binary operator per element and so blow
  // past clang's expression-nesting limit (256) for large search windows. The
  // leading empty-check keeps first-match-wins semantics.
  [[maybe_unused]] std::initializer_list<int> const expansion{
    (result.empty() && target == static_cast<std::underlying_type_t<E>>(Min + Is)
       ? (result = enum_value_name<static_cast<E>(Min + Is)>(), 0)
       : 0)...
  };
  return result;
}

/**
 * @brief Counts the named enumerators across the scan window.
 *
 * Expands the index pack into a flat braced-init sequence (not a sum fold,
 * which would exceed Clang's expression-nesting limit), counting each value
 * from \p Min upward that names a real enumerator.
 *
 * @tparam E Enum type being reflected.
 * @tparam Min First underlying value the index pack maps to.
 * @tparam Is Index pack offsetting \p Min across the scan window.
 *
 * @return The number of named enumerators in the window.
 *
 * @pre None.
 * @post None.
 */
template <typename E, int Min, int... Is>
[[nodiscard]] constexpr auto
enum_count_impl(std::integer_sequence<int, Is...>) noexcept -> std::size_t {
  auto count{std::size_t{0}};
  // Flat braced-init expansion rather than a sum fold, which would nest one
  // operator per element and exceed clang's expression-nesting limit.
  [[maybe_unused]] std::initializer_list<int> const expansion{
    (count +=
     (enum_value_name<static_cast<E>(Min + Is)>().empty() ? std::size_t{0} : std::size_t{1}),
     0)...
  };
  return count;
}

/**
 * @brief Fills \p out with the named enumerators across the scan window.
 *
 * Expands the index pack into a flat braced-init sequence (evaluated
 * left-to-right, so values pack in ascending order), writing each named
 * enumerator, value from \p Min upward, into the next slot of \p out.
 *
 * @tparam E Enum type being reflected.
 * @tparam Min First underlying value the index pack maps to.
 * @tparam N Size of the output array.
 * @tparam Is Index pack offsetting \p Min across the scan window.
 * @param out Array to fill with the named enumerators.
 *
 * @pre \p N equals the number of named enumerators in the window.
 * @post \p out holds the named enumerators in ascending underlying-value order.
 */
template <typename E, int Min, std::size_t N, int... Is>
constexpr auto
enum_values_impl(std::array<E, N>& out, std::integer_sequence<int, Is...>) noexcept -> void {
  auto i{std::size_t{0}};
  // Flat braced-init expansion (evaluated left-to-right, so values pack in
  // order) rather than a comma fold, which would exceed clang's nesting limit.
  [[maybe_unused]] std::initializer_list<int> const expansion{(
    enum_value_name<static_cast<E>(Min + Is)>().empty() ? 0
                                                        : (out[i++] = static_cast<E>(Min + Is), 0)
  )...};
}

/**
 * @brief Finds the enumerator whose name equals \p name across the window.
 *
 * Expands the index pack into a flat braced-init sequence (not an \c || fold,
 * which would exceed Clang's expression-nesting limit) and returns the first
 * enumerator, value from \p Min upward, whose name matches.
 *
 * @tparam E Enum type being produced.
 * @tparam Min First underlying value the index pack maps to.
 * @tparam Is Index pack offsetting \p Min across the scan window.
 * @param name Enumerator name to look up.
 *
 * @return The matching enumerator, or \c std::nullopt when none matches.
 *
 * @pre None.
 * @post None.
 */
template <typename E, int Min, int... Is>
[[nodiscard]] constexpr auto
enum_cast_impl(std::string_view const name, std::integer_sequence<int, Is...>) noexcept
  -> std::optional<E> {
  auto result{std::optional<E>{}};
  // Flat braced-init expansion rather than an || fold, which would exceed
  // clang's expression-nesting limit; the guard keeps first-match-wins.
  [[maybe_unused]] std::initializer_list<int> const expansion{
    (!result && enum_name_matches<static_cast<E>(Min + Is)>(name)
       ? (result = static_cast<E>(Min + Is), 0)
       : 0)...
  };
  return result;
}

/// @endcond

}  // namespace detail

/**
 * @brief Compile-time name of a single enumerator value.
 *
 * @tparam V A literal enumerator value of some enum type.
 *
 * @return The enumerator's name, or an empty view when \p V is not a named
 *         enumerator or the compiler is unsupported.
 *
 * @pre None.
 * @post None.
 *
 * @warning Requires GCC or Clang; other compilers return an empty view.
 */
template <auto V>
  requires enumeration<decltype(V)>
[[nodiscard]] constexpr auto enum_name() noexcept -> std::string_view {
  return detail::enum_value_name<V>();
}

/**
 * @brief Runtime enumerator-to-string lookup over a bounded value range.
 *
 * Linearly searches the underlying values \c [Min, Min + Range) for \p value
 * and returns its enumerator name. The window is clamped to the values
 * representable by \p E's underlying type, so an oversized \p Range never
 * wraps a narrow underlying type or scans a value twice.
 *
 * @tparam Range Number of underlying values to scan. Defaults to 256.
 * @tparam Min First underlying value to scan. Defaults to 0.
 * @tparam E Enum type of \p value.
 * @param value Enumerator to name.
 *
 * @return The matching enumerator name, or an empty view when none in range
 *         matches.
 *
 * @pre \p E has a fixed underlying type (every scoped enum does; an unscoped
 *      enum needs an explicit enum-base).
 * @post None.
 *
 * @complexity \c O(Range) comparisons.
 *
 * @warning For an enum without a fixed underlying type, casting a scanned
 *          value outside its range of values is undefined behaviour and Clang
 *          rejects the constant evaluation with a hard error; no trait can
 *          detect the missing enum-base, so this cannot be checked here.
 * @warning Requires GCC or Clang; other compilers return an empty view.
 */
template <int Range = 256, int Min = 0, enumeration E>
[[nodiscard]] constexpr auto enum_to_string(E const value) noexcept -> std::string_view {
  constexpr auto window{detail::clamped_window<E>(Min, Range)};
  return detail::enum_search<E, window.min>(
    static_cast<std::underlying_type_t<E>>(value), std::make_integer_sequence<int, window.range>{}
  );
}

/**
 * @brief Compile-time count of named enumerators in a bounded range.
 *
 * Counts how many underlying values in \c [Min, Min + Range) name a real
 * enumerator. Holes, aliases, and unnamed placeholder values are not counted.
 * The window is clamped to the values representable by \p E's underlying type,
 * so an oversized \p Range never wraps a narrow underlying type and never
 * counts an enumerator twice.
 *
 * @tparam E Enum type to reflect.
 * @tparam Range Number of underlying values to scan. Defaults to 256.
 * @tparam Min First underlying value to scan. Defaults to 0.
 *
 * @return The number of named enumerators in \c [Min, Min + Range).
 *
 * @pre \p E has a fixed underlying type (every scoped enum does; an unscoped
 *      enum needs an explicit enum-base).
 * @post The result is at most \p Range.
 *
 * @complexity \c O(Range) signature parses, all at compile time.
 *
 * @warning For an enum without a fixed underlying type, casting a scanned
 *          value outside its range of values is undefined behaviour and Clang
 *          rejects the constant evaluation with a hard error; no trait can
 *          detect the missing enum-base, so this cannot be checked here.
 * @warning Requires GCC or Clang. Range-bounded: enumerators outside
 *          \c [Min, Min + Range) are ignored; widen \p Range and \p Min for
 *          enums beyond the default window.
 */
template <enumeration E, int Range = 256, int Min = 0>
[[nodiscard]] constexpr auto enum_count() noexcept -> std::size_t {
  constexpr auto window{detail::clamped_window<E>(Min, Range)};
  return detail::enum_count_impl<E, window.min>(std::make_integer_sequence<int, window.range>{});
}

/**
 * @brief Compile-time array of the named enumerators in a bounded range.
 *
 * Gathers every named enumerator in \c [Min, Min + Range) into a \c std::array
 * sized exactly to \c enum_count, in ascending underlying-value order. The
 * window is clamped to the values representable by \p E's underlying type, so
 * an oversized \p Range never wraps a narrow underlying type and never emits
 * an enumerator twice.
 *
 * @tparam E Enum type to reflect.
 * @tparam Range Number of underlying values to scan. Defaults to 256.
 * @tparam Min First underlying value to scan. Defaults to 0.
 *
 * @return A \c std::array of the named enumerators in ascending order.
 *
 * @pre \p E has a fixed underlying type (every scoped enum does; an unscoped
 *      enum needs an explicit enum-base).
 * @post The result holds exactly \c enum_count<E, Range, Min>() elements.
 *
 * @complexity \c O(Range) signature parses, all at compile time.
 *
 * @warning For an enum without a fixed underlying type, casting a scanned
 *          value outside its range of values is undefined behaviour and Clang
 *          rejects the constant evaluation with a hard error; no trait can
 *          detect the missing enum-base, so this cannot be checked here.
 * @warning Requires GCC or Clang. Range-bounded: enumerators outside the
 *          window are omitted.
 */
template <enumeration E, int Range = 256, int Min = 0>
[[nodiscard]] constexpr auto enum_values() noexcept -> std::array<E, enum_count<E, Range, Min>()> {
  constexpr auto window{detail::clamped_window<E>(Min, Range)};
  auto out{std::array<E, enum_count<E, Range, Min>()>{}};
  detail::enum_values_impl<E, window.min>(out, std::make_integer_sequence<int, window.range>{});
  return out;
}

/**
 * @brief Runtime string-to-enumerator lookup over a bounded value range.
 *
 * Returns the first enumerator in \c [Min, Min + Range) whose name equals
 * \p name, or \c std::nullopt when none matches. The window is clamped to the
 * values representable by \p E's underlying type, so an oversized \p Range
 * never wraps a narrow underlying type or scans a value twice.
 *
 * @tparam E Enum type to produce (named first, so call sites read
 *           \c enum_cast<color>("red")).
 * @tparam Range Number of underlying values to scan. Defaults to 256.
 * @tparam Min First underlying value to scan. Defaults to 0.
 * @param name Enumerator name to look up.
 *
 * @return The matching enumerator, or \c std::nullopt when none matches.
 *
 * @pre \p E has a fixed underlying type (every scoped enum does; an unscoped
 *      enum needs an explicit enum-base).
 * @post None.
 *
 * @complexity \c O(Range) comparisons.
 *
 * @warning For an enum without a fixed underlying type, casting a scanned
 *          value outside its range of values is undefined behaviour and Clang
 *          rejects the constant evaluation with a hard error; no trait can
 *          detect the missing enum-base, so this cannot be checked here.
 * @warning Requires GCC or Clang. Range-bounded: enumerators outside the
 *          window are never matched.
 */
template <enumeration E, int Range = 256, int Min = 0>
[[nodiscard]] constexpr auto enum_cast(std::string_view const name) noexcept -> std::optional<E> {
  constexpr auto window{detail::clamped_window<E>(Min, Range)};
  return detail::enum_cast_impl<E, window.min>(
    name, std::make_integer_sequence<int, window.range>{}
  );
}

}  // namespace nexenne::utility
