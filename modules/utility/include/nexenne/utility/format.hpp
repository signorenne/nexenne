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
 *
 * The stateful helpers print a one-line summary of what their accessors
 * expose: \c buffer_cursor its position and size, \c lazy its cached value or
 * whether it is still pending, \c non_null the address it holds,
 * \c unique_resource its ownership and handle, \c scope_guard and \c defer
 * whether the cleanup is armed, and \c function_ref and \c in_place_function
 * whether a callable is bound. A string spec pads that summary.
 */

#include <concepts>
#include <cstddef>
#include <format>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

#include <nexenne/utility/buffer_cursor.hpp>
#include <nexenne/utility/cobs.hpp>
#include <nexenne/utility/defer.hpp>
#include <nexenne/utility/flags.hpp>
#include <nexenne/utility/function_ref.hpp>
#include <nexenne/utility/in_place_function.hpp>
#include <nexenne/utility/lazy.hpp>
#include <nexenne/utility/non_null.hpp>
#include <nexenne/utility/scope_guard.hpp>
#include <nexenne/utility/static_string.hpp>
#include <nexenne/utility/strong_typedef.hpp>
#include <nexenne/utility/unique_resource.hpp>

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

namespace nexenne::utility {

/**
 * @brief Debug string for a \c buffer_cursor: its position and buffer size.
 *
 * Renders, for example, \c "buffer_cursor(position=2, size=8)".
 *
 * @tparam Byte Element type of the cursor.
 * @param cursor Cursor to describe.
 *
 * @return The description.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Byte>
[[nodiscard]] auto to_string(buffer_cursor<Byte> const& cursor) -> std::string {
  return std::format("buffer_cursor(position={}, size={})", cursor.position(), cursor.size());
}

/**
 * @brief Streams a \c buffer_cursor via its \c to_string.
 *
 * @tparam Byte Element type of the cursor.
 * @param os Output stream.
 * @param cursor Cursor to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p cursor has been written to \p os.
 */
template <typename Byte>
auto operator<<(std::ostream& os, buffer_cursor<Byte> const& cursor) -> std::ostream& {
  return os << to_string(cursor);
}

/**
 * @brief Debug string for a \c lazy: its cached value, or that it is pending.
 *
 * Never runs the factory. Before the first access it renders
 * \c "lazy(pending)"; afterwards it renders the cached value, for example
 * \c "lazy(42)", when the value type is formattable, and \c "lazy(ready)"
 * otherwise.
 *
 * @tparam Factory Factory type of the wrapper.
 * @param value Wrapper to describe.
 *
 * @return The description.
 *
 * @pre None.
 * @post The factory has not run as a result of this call.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Factory>
[[nodiscard]] auto to_string(lazy<Factory> const& value) -> std::string {
  if (!value.has_value()) {
    return std::string{"lazy(pending)"};
  }
  if constexpr (std::formattable<typename lazy<Factory>::value_type, char>) {
    return std::format("lazy({})", value.get());
  } else {
    return std::string{"lazy(ready)"};
  }
}

/**
 * @brief Streams a \c lazy via its \c to_string.
 *
 * @tparam Factory Factory type of the wrapper.
 * @param os Output stream.
 * @param value Wrapper to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p value has been written to \p os; the factory has
 *       not run as a result of this call.
 */
template <typename Factory>
auto operator<<(std::ostream& os, lazy<Factory> const& value) -> std::ostream& {
  return os << to_string(value);
}

/**
 * @brief Debug string for a \c non_null: the address it holds.
 *
 * Renders, for example, \c "non_null(0x7ffc5a1e0b2c)", reading the address
 * through \c std::to_address so a smart pointer prints the object it points
 * at. A moved-from wrapper renders \c "non_null(null)" instead of asserting.
 *
 * @tparam T Wrapped pointer type, pointing at an object type.
 * @param ptr Wrapper to describe.
 *
 * @return The description.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename T>
  requires std::is_object_v<typename non_null<T>::element_type>
[[nodiscard]] auto to_string(non_null<T> const& ptr) -> std::string {
  if (ptr == nullptr) {
    return std::string{"non_null(null)"};
  }
  return std::format("non_null({})", static_cast<void const*>(std::to_address(ptr.get())));
}

/**
 * @brief Streams a \c non_null via its \c to_string.
 *
 * @tparam T Wrapped pointer type, pointing at an object type.
 * @param os Output stream.
 * @param ptr Wrapper to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p ptr has been written to \p os.
 */
template <typename T>
  requires std::is_object_v<typename non_null<T>::element_type>
auto operator<<(std::ostream& os, non_null<T> const& ptr) -> std::ostream& {
  return os << to_string(ptr);
}

/**
 * @brief Debug string for a \c unique_resource: its ownership and handle.
 *
 * Renders, for example, \c "unique_resource(owns=true, handle=3)". A pointer
 * handle prints its address, never the text a character pointer would point
 * at; a handle that is neither a pointer nor formattable is left out, as in
 * \c "unique_resource(owns=false)". The handle is the one \c get() reports,
 * so after \c release or \c reset it is the surrendered or released value.
 *
 * @tparam Resource Handle type of the owner.
 * @tparam Deleter Deleter type of the owner.
 * @param resource Owner to describe.
 *
 * @return The description.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Resource, typename Deleter>
[[nodiscard]] auto to_string(unique_resource<Resource, Deleter> const& resource) -> std::string {
  if constexpr (std::is_pointer_v<Resource>
                && !std::is_function_v<std::remove_pointer_t<Resource>>) {
    return std::format(
      "unique_resource(owns={}, handle={})",
      resource.owns(),
      static_cast<void const*>(resource.get())
    );
  } else if constexpr (std::formattable<Resource, char>) {
    return std::format("unique_resource(owns={}, handle={})", resource.owns(), resource.get());
  } else {
    return std::format("unique_resource(owns={})", resource.owns());
  }
}

/**
 * @brief Streams a \c unique_resource via its \c to_string.
 *
 * @tparam Resource Handle type of the owner.
 * @tparam Deleter Deleter type of the owner.
 * @param os Output stream.
 * @param resource Owner to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p resource has been written to \p os.
 */
template <typename Resource, typename Deleter>
auto operator<<(std::ostream& os, unique_resource<Resource, Deleter> const& resource)
  -> std::ostream& {
  return os << to_string(resource);
}

/**
 * @brief Debug string for a \c scope_guard: whether its cleanup is armed.
 *
 * Renders \c "scope_guard(armed)" while \c is_active() is \c true and
 * \c "scope_guard(dismissed)" after \c dismiss.
 *
 * @tparam Fn Cleanup callable type of the guard.
 * @param guard Guard to describe.
 *
 * @return The description.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Fn>
[[nodiscard]] auto to_string(scope_guard<Fn> const& guard) -> std::string {
  return std::string{guard.is_active() ? "scope_guard(armed)" : "scope_guard(dismissed)"};
}

/**
 * @brief Streams a \c scope_guard via its \c to_string.
 *
 * @tparam Fn Cleanup callable type of the guard.
 * @param os Output stream.
 * @param guard Guard to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p guard has been written to \p os.
 */
template <typename Fn>
auto operator<<(std::ostream& os, scope_guard<Fn> const& guard) -> std::ostream& {
  return os << to_string(guard);
}

/**
 * @brief Debug string for a \c defer: always \c "defer(armed)".
 *
 * A \c defer cannot be dismissed, so its cleanup is armed for its whole
 * lifetime; the text names that state so a \c defer prints alongside a
 * \c scope_guard.
 *
 * @tparam Fn Cleanup callable type of the guard.
 * @param guard Guard to describe.
 *
 * @return The description.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Fn>
[[nodiscard]] auto to_string([[maybe_unused]] defer<Fn> const& guard) -> std::string {
  return std::string{"defer(armed)"};
}

/**
 * @brief Streams a \c defer via its \c to_string.
 *
 * @tparam Fn Cleanup callable type of the guard.
 * @param os Output stream.
 * @param guard Guard to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p guard has been written to \p os.
 */
template <typename Fn>
auto operator<<(std::ostream& os, defer<Fn> const& guard) -> std::ostream& {
  return os << to_string(guard);
}

/**
 * @brief Debug string for a \c function_ref: whether it refers to a callable.
 *
 * Renders \c "function_ref(bound)" or \c "function_ref(empty)".
 *
 * @tparam Sig Function signature of the view.
 * @param fn View to describe.
 *
 * @return The description.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Sig>
[[nodiscard]] auto to_string(function_ref<Sig> const& fn) -> std::string {
  return std::string{fn ? "function_ref(bound)" : "function_ref(empty)"};
}

/**
 * @brief Streams a \c function_ref via its \c to_string.
 *
 * @tparam Sig Function signature of the view.
 * @param os Output stream.
 * @param fn View to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p fn has been written to \p os.
 */
template <typename Sig>
auto operator<<(std::ostream& os, function_ref<Sig> const& fn) -> std::ostream& {
  return os << to_string(fn);
}

/**
 * @brief Debug string for an \c in_place_function: whether it holds a callable.
 *
 * Renders, for example, \c "in_place_function(bound, capacity=64)" or
 * \c "in_place_function(empty, capacity=64)", with the inline storage size in
 * bytes.
 *
 * @tparam Sig Function signature of the wrapper.
 * @tparam Capacity Inline storage in bytes.
 * @param fn Wrapper to describe.
 *
 * @return The description.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
template <typename Sig, std::size_t Capacity>
[[nodiscard]] auto to_string(in_place_function<Sig, Capacity> const& fn) -> std::string {
  return std::format("in_place_function({}, capacity={})", fn ? "bound" : "empty", Capacity);
}

/**
 * @brief Streams an \c in_place_function via its \c to_string.
 *
 * @tparam Sig Function signature of the wrapper.
 * @tparam Capacity Inline storage in bytes.
 * @param os Output stream.
 * @param fn Wrapper to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p fn has been written to \p os.
 */
template <typename Sig, std::size_t Capacity>
auto operator<<(std::ostream& os, in_place_function<Sig, Capacity> const& fn) -> std::ostream& {
  return os << to_string(fn);
}

}  // namespace nexenne::utility

/**
 * @brief \c std::formatter specialisation printing a \c buffer_cursor summary.
 *
 * Forwards to \c to_string, so \c {} yields, for example,
 * \c "buffer_cursor(position=2, size=8)" and a string spec pads the text.
 *
 * @tparam Byte Element type of the cursor.
 *
 * @pre None.
 * @post None.
 */
template <typename Byte>
struct std::formatter<nexenne::utility::buffer_cursor<Byte>, char>
    : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p cursor by writing its \c to_string text.
   *
   * @tparam Context Formatting context type.
   * @param cursor Cursor to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   *
   * @throws std::bad_alloc if building the text fails.
   */
  template <typename Context>
  auto format(nexenne::utility::buffer_cursor<Byte> const& cursor, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(nexenne::utility::to_string(cursor), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing a \c lazy summary.
 *
 * Forwards to \c to_string, so \c {} yields the cached value, for example
 * \c "lazy(42)", or \c "lazy(pending)" before the first access; it never runs
 * the factory.
 *
 * @tparam Factory Factory type of the wrapper.
 *
 * @pre None.
 * @post None.
 */
template <typename Factory>
struct std::formatter<nexenne::utility::lazy<Factory>, char>
    : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p value by writing its \c to_string text.
   *
   * @tparam Context Formatting context type.
   * @param value Wrapper to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post The factory has not run as a result of this call.
   *
   * @throws std::bad_alloc if building the text fails.
   */
  template <typename Context>
  auto format(nexenne::utility::lazy<Factory> const& value, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(nexenne::utility::to_string(value), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing the address a \c non_null holds.
 *
 * Forwards to \c to_string, so \c {} yields, for example,
 * \c "non_null(0x7ffc5a1e0b2c)".
 *
 * @tparam T Wrapped pointer type, pointing at an object type.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
  requires std::is_object_v<typename nexenne::utility::non_null<T>::element_type>
struct std::formatter<nexenne::utility::non_null<T>, char>
    : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p ptr by writing its \c to_string text.
   *
   * @tparam Context Formatting context type.
   * @param ptr Wrapper to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   *
   * @throws std::bad_alloc if building the text fails.
   */
  template <typename Context>
  auto format(nexenne::utility::non_null<T> const& ptr, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(nexenne::utility::to_string(ptr), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing a \c unique_resource summary.
 *
 * Forwards to \c to_string, so \c {} yields, for example,
 * \c "unique_resource(owns=true, handle=3)".
 *
 * @tparam Resource Handle type of the owner.
 * @tparam Deleter Deleter type of the owner.
 *
 * @pre None.
 * @post None.
 */
template <typename Resource, typename Deleter>
struct std::formatter<nexenne::utility::unique_resource<Resource, Deleter>, char>
    : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p resource by writing its \c to_string text.
   *
   * @tparam Context Formatting context type.
   * @param resource Owner to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   *
   * @throws std::bad_alloc if building the text fails.
   */
  template <typename Context>
  auto
  format(nexenne::utility::unique_resource<Resource, Deleter> const& resource, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(
      nexenne::utility::to_string(resource), ctx
    );
  }
};

/**
 * @brief \c std::formatter specialisation printing whether a \c scope_guard is armed.
 *
 * Forwards to \c to_string, so \c {} yields \c "scope_guard(armed)" or
 * \c "scope_guard(dismissed)".
 *
 * @tparam Fn Cleanup callable type of the guard.
 *
 * @pre None.
 * @post None.
 */
template <typename Fn>
struct std::formatter<nexenne::utility::scope_guard<Fn>, char>
    : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p guard by writing its \c to_string text.
   *
   * @tparam Context Formatting context type.
   * @param guard Guard to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   *
   * @throws std::bad_alloc if building the text fails.
   */
  template <typename Context>
  auto format(nexenne::utility::scope_guard<Fn> const& guard, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(nexenne::utility::to_string(guard), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing a \c defer as armed.
 *
 * Forwards to \c to_string, so \c {} yields \c "defer(armed)".
 *
 * @tparam Fn Cleanup callable type of the guard.
 *
 * @pre None.
 * @post None.
 */
template <typename Fn>
struct std::formatter<nexenne::utility::defer<Fn>, char> : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p guard by writing its \c to_string text.
   *
   * @tparam Context Formatting context type.
   * @param guard Guard to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   *
   * @throws std::bad_alloc if building the text fails.
   */
  template <typename Context>
  auto format(nexenne::utility::defer<Fn> const& guard, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(nexenne::utility::to_string(guard), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing whether a \c function_ref is bound.
 *
 * Forwards to \c to_string, so \c {} yields \c "function_ref(bound)" or
 * \c "function_ref(empty)".
 *
 * @tparam Sig Function signature of the view.
 *
 * @pre None.
 * @post None.
 */
template <typename Sig>
struct std::formatter<nexenne::utility::function_ref<Sig>, char>
    : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p fn by writing its \c to_string text.
   *
   * @tparam Context Formatting context type.
   * @param fn View to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   *
   * @throws std::bad_alloc if building the text fails.
   */
  template <typename Context>
  auto format(nexenne::utility::function_ref<Sig> const& fn, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(nexenne::utility::to_string(fn), ctx);
  }
};

/**
 * @brief \c std::formatter specialisation printing an \c in_place_function summary.
 *
 * Forwards to \c to_string, so \c {} yields, for example,
 * \c "in_place_function(bound, capacity=64)".
 *
 * @tparam Sig Function signature of the wrapper.
 * @tparam Capacity Inline storage in bytes.
 *
 * @pre None.
 * @post None.
 */
template <typename Sig, std::size_t Capacity>
struct std::formatter<nexenne::utility::in_place_function<Sig, Capacity>, char>
    : std::formatter<std::string_view, char> {
  /**
   * @brief Formats \p fn by writing its \c to_string text.
   *
   * @tparam Context Formatting context type.
   * @param fn Wrapper to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   *
   * @throws std::bad_alloc if building the text fails.
   */
  template <typename Context>
  auto format(nexenne::utility::in_place_function<Sig, Capacity> const& fn, Context& ctx) const {
    return std::formatter<std::string_view, char>::format(nexenne::utility::to_string(fn), ctx);
  }
};
