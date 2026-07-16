#pragma once

/**
 * @file
 * @brief Compile-time fixed-length string usable as a non-type template
 *        parameter (NTTP).
 *
 * C++20 allows non-type template parameters of structural type, which a
 * \c std::string_view or \c std::string is not, so \c template <std::string_view S>
 * is ill-formed. This wrapper closes the gap by storing the characters in a
 * public \c std::array (a structural type), so a string literal can be passed
 * as a template argument.
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <compare>
#include <cstddef>
#include <format>
#include <functional>
#include <string_view>

namespace nexenne::utility {

/**
 * @brief Compile-time fixed-length string usable as a non-type template parameter.
 *
 * Stores the characters (including the null terminator) in a public
 * \c std::array, making the type structural so it can be an NTTP. \p N is only
 * the capacity bound (the buffer size including the terminator); \c size() and
 * every query built on it report the content length, the number of characters
 * before the first NUL. The class invariant, established by every constructor,
 * is that the buffer contains at least one NUL byte.
 *
 * @tparam N Buffer size including the null terminator.
 *
 * @pre None.
 * @post A default-constructed instance is the empty string.
 *
 * @par Example
 * \code
 * template <nexenne::utility::static_string Name>
 * struct named {
 *   static constexpr auto name() noexcept { return Name.view(); }
 * };
 * static_assert(named<"hello">::name() == "hello");
 * \endcode
 */
template <std::size_t N>
  requires(N >= 1)
struct static_string {
  using value_type = char;

  std::array<value_type, N> data{};

  /**
   * @brief Constructs the empty string (an all-zero buffer).
   *
   * @pre None.
   * @post Every byte of \c data is zero; \c empty() returns \c true.
   */
  constexpr static_string() noexcept = default;

  /**
   * @brief Constructs from a null-terminated array, copying all \p N bytes.
   *
   * A string literal's length \p N (including the terminator) fixes the
   * template parameter via CTAD.
   *
   * @param str Source array of length \p N ending in a null terminator.
   *
   * @pre The last byte, \c str[N - 1], is the null terminator; a string
   *      literal always satisfies this.
   * @post \c data holds a copy of \p str including its terminator.
   */
  constexpr static_string(char const (&str)[N]) noexcept {  // NOLINT(hicpp-explicit-conversions)
    assert(str[N - 1] == '\0' && "static_string: source array must be null-terminated");
    std::copy_n(static_cast<char const*>(str), N, data.begin());
  }

  /**
   * @brief The content length: the number of characters before the first NUL.
   *
   * \p N is only the capacity bound; a partially filled buffer reports the
   * shorter content length, and a default-constructed instance reports zero.
   *
   * @return The index of the first NUL byte in the buffer.
   *
   * @pre None.
   * @post The result is at most \c N - 1.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    // The invariant guarantees a terminator inside the buffer (the default
    // constructor zero-fills, the array constructor asserts str[N - 1] is
    // NUL), so this constexpr scan is bounded and safe.
    return std::char_traits<value_type>::length(data.data());
  }

  /**
   * @brief Reports whether the string content is empty.
   *
   * @return \c true when the content length is zero, that is, when the buffer
   *         starts with a NUL byte.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return size() == 0;
  }

  /**
   * @brief A view over the string content (without the null terminator).
   *
   * @return A \c std::string_view spanning the first \c size() bytes.
   *
   * @pre None.
   * @post The view is valid as long as this object is alive.
   */
  [[nodiscard]] constexpr auto view() const noexcept -> std::string_view {
    return std::string_view{data.data(), size()};
  }

  /**
   * @brief A null-terminated pointer to the string contents.
   *
   * @return Pointer to the internal buffer.
   *
   * @pre None.
   * @post The pointer is valid as long as this object is alive.
   */
  [[nodiscard]] constexpr auto c_str() const noexcept -> char const* {
    return data.data();
  }

  /**
   * @brief The character at index \p i.
   *
   * @param i Index into the buffer.
   *
   * @return The character at position \p i.
   *
   * @pre \p i is less than \p N; a larger index asserts in debug and reads out
   *      of bounds in release.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](std::size_t const i) const noexcept -> value_type {
    assert(i < N && "static_string: index out of range");
    return data[i];
  }

  /**
   * @brief Iterator to the first character of the content.
   *
   * @return Pointer to the first character.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> value_type const* {
    return data.data();
  }

  /**
   * @brief Iterator one past the last content character.
   *
   * @return Pointer to the first NUL byte (one past the content).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> value_type const* {
    return data.data() + size();
  }

  /// @brief Equality of two equally sized static strings (compares buffers).
  [[nodiscard]] friend constexpr auto
  operator==(static_string const&, static_string const&) noexcept -> bool = default;

  /// @brief Lexicographic ordering of two equally sized static strings.
  [[nodiscard]] friend constexpr auto
  operator<=>(static_string const&, static_string const&) noexcept = default;

  /**
   * @brief Compile-time concatenation of two static strings.
   *
   * Concatenating \c "ab" with \c "cd" yields a \c static_string<5> holding
   * \c "abcd"; the result capacity is \c N + M - 1 (both buffers minus one
   * shared terminator). The contents concatenate, so partially filled operands
   * produce a result whose \c size() is the sum of the operand sizes.
   *
   * @tparam M Buffer size (including terminator) of the right operand.
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return A \c static_string<N + M - 1> holding \p a followed by \p b.
   *
   * @pre None.
   * @post The result content is the two contents concatenated and
   *       null-terminated; its \c size() is \c a.size() + b.size().
   */
  template <std::size_t M>
  [[nodiscard]] friend constexpr auto
  operator+(static_string const& a, static_string<M> const& b) noexcept
    -> static_string<N + M - 1> {
    static_string<N + M - 1> out{};
    // Copy the content runs, not the raw buffers: a partially filled left
    // operand must not push NUL padding between the two contents.
    std::copy_n(a.data.begin(), a.size(), out.data.begin());
    std::copy_n(b.data.begin(), b.size(), out.data.begin() + a.size());
    return out;
  }
};

/**
 * @brief Deduces \c static_string's \p N from a string literal.
 *
 * @tparam N Length of the literal including its null terminator.
 * @param str String literal whose length fixes \p N.
 *
 * @pre None.
 * @post \c static_string{"abc"} deduces \c static_string<4>.
 */
template <std::size_t N>
static_string(char const (&str)[N]) -> static_string<N>;

}  // namespace nexenne::utility

/**
 * @brief \c std::hash specialisation for \c static_string.
 *
 * Hashes the content through \c std::hash<std::string_view> so a
 * \c static_string can key an unordered container.
 *
 * @tparam N Buffer size of the static string.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N>
struct std::hash<nexenne::utility::static_string<N>> {
  /**
   * @brief Hashes \p s by hashing its content as a \c string_view.
   *
   * @param s Static string to hash.
   *
   * @return The hash of \c s.view().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto operator()(nexenne::utility::static_string<N> const& s) const noexcept
    -> std::size_t {
    return std::hash<std::string_view>{}(s.view());
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
