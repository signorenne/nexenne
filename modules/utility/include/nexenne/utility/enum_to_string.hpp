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
 * Requires GCC or Clang; other compilers see no named enumerators.
 */

#include <array>
#include <cstddef>
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

template <auto V>
[[nodiscard]] constexpr auto enum_name_matches(std::string_view const name) noexcept -> bool {
  auto const candidate{enum_value_name<V>()};
  return !candidate.empty() && candidate == name;
}

// The integer sequences below are built over int, not the underlying type, to
// avoid overflow when the underlying type is narrow: a
// std::make_integer_sequence<std::uint8_t, 256> would silently produce zero
// elements because 256 is not representable.

template <typename E, int Min, int... Is>
[[nodiscard]] constexpr auto
enum_search(std::underlying_type_t<E> const target, std::integer_sequence<int, Is...>) noexcept
  -> std::string_view {
  auto result{std::string_view{}};
  [[maybe_unused]] auto const matched{
    ((target == static_cast<std::underlying_type_t<E>>(Min + Is)
        ? (result = enum_value_name<static_cast<E>(Min + Is)>(), true)
        : false)
     || ...)
  };
  return result;
}

template <typename E, int Min, int... Is>
[[nodiscard]] constexpr auto
enum_count_impl(std::integer_sequence<int, Is...>) noexcept -> std::size_t {
  return (
    std::size_t{0} + ...
    + (enum_value_name<static_cast<E>(Min + Is)>().empty() ? std::size_t{0} : std::size_t{1})
  );
}

template <typename E, int Min, std::size_t N, int... Is>
constexpr auto
enum_values_impl(std::array<E, N>& out, std::integer_sequence<int, Is...>) noexcept -> void {
  auto i{std::size_t{0}};
  ((enum_value_name<static_cast<E>(Min + Is)>().empty()
      ? static_cast<void>(0)
      : static_cast<void>(out[i++] = static_cast<E>(Min + Is))),
   ...);
}

template <typename E, int Min, int... Is>
[[nodiscard]] constexpr auto
enum_cast_impl(std::string_view const name, std::integer_sequence<int, Is...>) noexcept
  -> std::optional<E> {
  auto result{std::optional<E>{}};
  [[maybe_unused]] auto const matched{
    ((enum_name_matches<static_cast<E>(Min + Is)>(name) ? (result = static_cast<E>(Min + Is), true)
                                                        : false)
     || ...)
  };
  return result;
}

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
 * and returns its enumerator name.
 *
 * @tparam Range Number of underlying values to scan. Defaults to 256.
 * @tparam Min First underlying value to scan. Defaults to 0.
 * @tparam E Enum type of \p value.
 * @param value Enumerator to name.
 *
 * @return The matching enumerator name, or an empty view when none in range
 *         matches.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(Range) comparisons.
 *
 * @warning Requires GCC or Clang; other compilers return an empty view.
 */
template <int Range = 256, int Min = 0, enumeration E>
[[nodiscard]] constexpr auto enum_to_string(E const value) noexcept -> std::string_view {
  return detail::enum_search<E, Min>(
    static_cast<std::underlying_type_t<E>>(value), std::make_integer_sequence<int, Range>{}
  );
}

/**
 * @brief Compile-time count of named enumerators in a bounded range.
 *
 * Counts how many underlying values in \c [Min, Min + Range) name a real
 * enumerator. Holes, aliases, and unnamed placeholder values are not counted.
 *
 * @tparam E Enum type to reflect.
 * @tparam Range Number of underlying values to scan. Defaults to 256.
 * @tparam Min First underlying value to scan. Defaults to 0.
 *
 * @return The number of named enumerators in \c [Min, Min + Range).
 *
 * @pre None.
 * @post The result is at most \p Range.
 *
 * @complexity \c O(Range) signature parses, all at compile time.
 *
 * @warning Requires GCC or Clang. Range-bounded: enumerators outside
 *          \c [Min, Min + Range) are ignored; widen \p Range and \p Min for
 *          enums beyond the default window.
 */
template <enumeration E, int Range = 256, int Min = 0>
[[nodiscard]] constexpr auto enum_count() noexcept -> std::size_t {
  return detail::enum_count_impl<E, Min>(std::make_integer_sequence<int, Range>{});
}

/**
 * @brief Compile-time array of the named enumerators in a bounded range.
 *
 * Gathers every named enumerator in \c [Min, Min + Range) into a \c std::array
 * sized exactly to \c enum_count, in ascending underlying-value order.
 *
 * @tparam E Enum type to reflect.
 * @tparam Range Number of underlying values to scan. Defaults to 256.
 * @tparam Min First underlying value to scan. Defaults to 0.
 *
 * @return A \c std::array of the named enumerators in ascending order.
 *
 * @pre None.
 * @post The result holds exactly \c enum_count<E, Range, Min>() elements.
 *
 * @complexity \c O(Range) signature parses, all at compile time.
 *
 * @warning Requires GCC or Clang. Range-bounded: enumerators outside the
 *          window are omitted.
 */
template <enumeration E, int Range = 256, int Min = 0>
[[nodiscard]] constexpr auto enum_values() noexcept -> std::array<E, enum_count<E, Range, Min>()> {
  auto out{std::array<E, enum_count<E, Range, Min>()>{}};
  detail::enum_values_impl<E, Min>(out, std::make_integer_sequence<int, Range>{});
  return out;
}

/**
 * @brief Runtime string-to-enumerator lookup over a bounded value range.
 *
 * Returns the first enumerator in \c [Min, Min + Range) whose name equals
 * \p name, or \c std::nullopt when none matches.
 *
 * @tparam E Enum type to produce (named first, so call sites read
 *           \c enum_cast<color>("red")).
 * @tparam Range Number of underlying values to scan. Defaults to 256.
 * @tparam Min First underlying value to scan. Defaults to 0.
 * @param name Enumerator name to look up.
 *
 * @return The matching enumerator, or \c std::nullopt when none matches.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(Range) comparisons.
 *
 * @warning Requires GCC or Clang. Range-bounded: enumerators outside the
 *          window are never matched.
 */
template <enumeration E, int Range = 256, int Min = 0>
[[nodiscard]] constexpr auto enum_cast(std::string_view const name) noexcept -> std::optional<E> {
  return detail::enum_cast_impl<E, Min>(name, std::make_integer_sequence<int, Range>{});
}

}  // namespace nexenne::utility
