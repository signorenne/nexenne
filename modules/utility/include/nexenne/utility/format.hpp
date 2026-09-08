#pragma once

/**
 * @file
 * @brief \c std::formatter specialisations for the nexenne::utility types.
 *
 * Kept out of the type headers because the \c format standard header is heavy
 * and those headers are included across the library. Include this header (or
 * the \c utility.hpp umbrella) wherever \c std::format prints a COBS \c error,
 * a \c flags set, a
 * \c static_string, a \c strong_typedef or an \c ability. Format specs pass
 * through to the underlying formatter: a string spec such as "{:>8}" for the
 * named and string types, an integer or floating spec for \c flags and
 * \c strong_typedef.
 */

#include <concepts>
#include <cstddef>
#include <format>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

#include <nexenne/utility/cobs.hpp>
#include <nexenne/utility/flags.hpp>
#include <nexenne/utility/static_string.hpp>
#include <nexenne/utility/strong_typedef.hpp>

/// @cond INTERNAL
namespace nexenne::utility::flag_naming {

/**
 * @brief Poison pill: hides every enclosing \c to_string from the name probe.
 *
 * Only argument-dependent lookup can then find a \c to_string for the enum,
 * so a flag set's own \c to_string never answers for its bits.
 */
auto to_string() -> void = delete;

/**
 * @brief An enum whose values have names.
 *
 * \c to_string(e), found by argument-dependent lookup, returns a
 * \c std::string_view.
 *
 * @tparam E Enumeration to probe.
 */
template <typename E>
concept named_enum = requires(E const e) {
  { to_string(e) } -> std::same_as<std::string_view>;
};

/**
 * @brief The names of the bits set in \p value, joined by single spaces.
 *
 * @tparam E Named scoped enumeration of the set.
 * @param value Flag set to name.
 *
 * @return The names in ascending bit order; empty for an empty set.
 *
 * @pre Every set bit of \p value is an enumerator of \p E.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot grow.
 */
template <named_enum E>
[[nodiscard]] auto joined_names(flags<E> const value) -> std::string {
  using unsigned_type = typename flags<E>::unsigned_type;
  auto const bits{static_cast<unsigned_type>(value.raw())};
  auto out{std::string{}};
  for (auto i{0}; i < std::numeric_limits<unsigned_type>::digits; ++i) {
    auto const bit{static_cast<unsigned_type>(unsigned_type{1} << static_cast<unsigned>(i))};
    if ((bits & bit) == 0) {
      continue;
    }
    if (!out.empty()) {
      out += ' ';
    }
    out += to_string(static_cast<E>(bit));
  }
  return out;
}

}  // namespace nexenne::utility::flag_naming

/// @endcond

/**
 * @brief \c std::formatter specialisation printing a COBS \c error by its name.
 *
 * Forwards to \c to_string, so \c std::format("{}", error::invalid_input)
 * yields \c "invalid_input" and a string spec such as "{:>16}" pads the name.
 */
template <>
struct std::formatter<nexenne::utility::cobs::error> : std::formatter<std::string_view> {
  /**
   * @brief Formats \p e by writing its name.
   *
   * @tparam Context Formatting context type.
   * @param e Error code to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   */
  template <typename Context>
  auto format(nexenne::utility::cobs::error const e, Context& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::utility::cobs::to_string(e), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing the raw mask of an unnamed
 * flag set.
 *
 * Used when the enum has no \c to_string (a set of a named enum prints the
 * names of its bits instead, below).
 * Formats the raw bits converted to the unsigned counterpart of the underlying
 * type and inherits that integer formatter, so format specs pass straight
 * through: \c {} prints the mask in decimal, \c {:\#b} in binary, \c {:\#x} in
 * hex. A signed underlying type prints its two's-complement bit pattern
 * rather than a sign.
 *
 * @tparam E Scoped enum type of the flag set.
 * @tparam CharT Character type of the format context.
 */
template <nexenne::utility::scoped_enum E, typename CharT>
  requires(!nexenne::utility::flag_naming::named_enum<E>)
struct std::formatter<nexenne::utility::flags<E>, CharT>
    : std::formatter<typename nexenne::utility::flags<E>::unsigned_type, CharT> {
  /**
   * @brief Formats \p value by formatting its raw bits as an unsigned integer.
   *
   * @tparam Context Formatting context type.
   * @param value Flag set to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   */
  template <typename Context>
  auto format(nexenne::utility::flags<E> const value, Context& ctx) const {
    using unsigned_type = typename nexenne::utility::flags<E>::unsigned_type;
    return std::formatter<unsigned_type, CharT>::format(
      static_cast<unsigned_type>(value.raw()), ctx
    );
  }
};

/**
 * @brief \c std::formatter specialisation printing a named flag set's bit
 * names.
 *
 * Used when an argument-dependent \c to_string(E) returns a
 * \c std::string_view: \c {} prints the set bits' names joined by spaces (for
 * example \c "fdf brs"), and a string spec such as "{:>12}" pads the text. Call
 * \c raw() to format the bits as a number instead.
 *
 * @tparam E Named scoped enum type of the flag set.
 */
template <nexenne::utility::flag_naming::named_enum E>
  requires nexenne::utility::scoped_enum<E>
struct std::formatter<nexenne::utility::flags<E>, char> : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p value as the names of its set bits.
   *
   * @tparam Context Formatting context type.
   * @param value Flag set to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre Every set bit of \p value is an enumerator of \p E.
   * @post None.
   *
   * @throws std::bad_alloc if building the name string fails.
   */
  template <typename Context>
  auto format(nexenne::utility::flags<E> const value, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(
      nexenne::utility::flag_naming::joined_names(value), ctx
    );
  }
};

/**
 * @brief \c std::formatter specialisation for \c static_string.
 *
 * Formats the body with full string format-spec support (for example "{:>8}")
 * by inheriting the \c std::string_view formatter.
 *
 * @tparam N Buffer size of the static string.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N>
struct std::formatter<nexenne::utility::static_string<N>, char>
    : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p s by formatting its body as a \c string_view.
   *
   * @tparam Context Formatting context type.
   * @param s Static string to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post The body has been written into \p ctx using the inherited spec.
   */
  template <typename Context>
  auto format(nexenne::utility::static_string<N> const& s, Context& ctx) const
    -> decltype(ctx.out()) {
    return std::formatter<std::string_view, char>::format(s.view(), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation inheriting the underlying type's
 * formatter.
 *
 * Format specs such as "{:>8}" or "{:.2f}" pass straight through to \p T.
 *
 * @tparam Tag Tag type of the wrapper.
 * @tparam T Underlying value type.
 * @tparam Ops Capability set of the wrapper.
 * @tparam CharT Character type of the format context.
 */
template <typename Tag, typename T, nexenne::utility::ability Ops, typename CharT>
struct std::formatter<nexenne::utility::strong_typedef<Tag, T, Ops>, CharT>
    : std::formatter<T, CharT> {
  /**
   * @brief Formats \p value by formatting its underlying value.
   *
   * @tparam Context Formatting context type.
   * @param value Wrapper to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   */
  template <typename Context>
  auto format(nexenne::utility::strong_typedef<Tag, T, Ops> const& value, Context& ctx) const {
    return std::formatter<T, CharT>::format(value.get(), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing an \c ability by its name.
 *
 * Forwards to \c to_string, so \c std::format("{}", ability::scale) yields
 * \c "scale" and a string spec such as "{:>10}" pads the name.
 */
template <>
struct std::formatter<nexenne::utility::ability> : std::formatter<std::string_view> {
  /**
   * @brief Formats \p flag by writing its name.
   *
   * @tparam Context Formatting context type.
   * @param flag Flag, named group or \c none to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   */
  template <typename Context>
  auto format(nexenne::utility::ability flag, Context& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::utility::to_string(flag), ctx);
  }
};

namespace nexenne::utility {

/**
 * @brief Streams an \c ability by its name.
 *
 * @param os Output stream.
 * @param flag Ability to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p flag has been written to \p os.
 */
inline auto operator<<(std::ostream& os, ability const flag) -> std::ostream& {
  return os << to_string(flag);
}

/**
 * @brief Debug string for a flag set: its bit names, or its raw mask.
 *
 * A set of a named enum (an argument-dependent \c to_string(E) returning a
 * \c std::string_view) renders the names of its set bits joined by spaces, an
 * empty set as the empty string; any other set renders its mask in decimal, as
 * \c std::format("{}") does.
 *
 * @tparam E Scoped enum type of the set.
 * @param value Flag set to describe.
 *
 * @return The description.
 *
 * @pre For a named enum, every set bit is an enumerator of \p E.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <scoped_enum E>
[[nodiscard]] auto to_string(flags<E> const value) -> std::string {
  if constexpr (flag_naming::named_enum<E>) {
    return flag_naming::joined_names(value);
  } else {
    return std::format("{}", value);
  }
}

/**
 * @brief Streams a flag set via its \c to_string.
 *
 * @tparam E Scoped enum type of the set.
 * @param os Output stream.
 * @param value Flag set to print.
 *
 * @return Reference to \p os.
 *
 * @pre As for \c to_string.
 * @post The description of \p value has been written to \p os.
 */
template <scoped_enum E>
auto operator<<(std::ostream& os, flags<E> const value) -> std::ostream& {
  return os << to_string(value);
}

/**
 * @brief The text of a \c static_string as a \c std::string.
 *
 * @tparam N Capacity of the string.
 * @param s String to copy.
 *
 * @return A copy of the stored characters.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <std::size_t N>
[[nodiscard]] auto to_string(static_string<N> const& s) -> std::string {
  return std::string{s.view()};
}

/**
 * @brief Streams a \c static_string's text.
 *
 * @tparam N Capacity of the string.
 * @param os Output stream.
 * @param s String to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The characters of \p s have been written to \p os.
 */
template <std::size_t N>
auto operator<<(std::ostream& os, static_string<N> const& s) -> std::ostream& {
  return os << s.view();
}

/**
 * @brief Debug string for a \c strong_typedef: its underlying value, formatted.
 *
 * @tparam Tag Tag type of the wrapper.
 * @tparam T Underlying value type, formattable.
 * @tparam Ops Capability set of the wrapper.
 * @param value Wrapper to describe.
 *
 * @return \c std::format("{}", value.get()).
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Tag, std::formattable<char> T, ability Ops>
[[nodiscard]] auto to_string(strong_typedef<Tag, T, Ops> const& value) -> std::string {
  return std::format("{}", value.get());
}

/**
 * @brief Streams a \c strong_typedef via its \c to_string.
 *
 * @tparam Tag Tag type of the wrapper.
 * @tparam T Underlying value type, formattable.
 * @tparam Ops Capability set of the wrapper.
 * @param os Output stream.
 * @param value Wrapper to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The underlying value of \p value has been written to \p os.
 */
template <typename Tag, std::formattable<char> T, ability Ops>
auto operator<<(std::ostream& os, strong_typedef<Tag, T, Ops> const& value) -> std::ostream& {
  return os << to_string(value);
}

}  // namespace nexenne::utility

namespace nexenne::utility::cobs {

/**
 * @brief Streams a COBS \c error by its name.
 *
 * @param os Output stream.
 * @param e Error code to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p e has been written to \p os.
 */
inline auto operator<<(std::ostream& os, error const e) -> std::ostream& {
  return os << to_string(e);
}

}  // namespace nexenne::utility::cobs
