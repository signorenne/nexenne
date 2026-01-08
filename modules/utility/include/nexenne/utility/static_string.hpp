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
 * \c std::array, making the type structural so it can be an NTTP. Size queries
 * report the length excluding the terminator. \p N is the buffer size
 * including the terminator.
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
  std::array<char, N> data{};

  /**
   * @brief Constructs the empty string (an all-zero buffer).
   *
   * @pre None.
   * @post Every byte of \c data is zero; \c empty() returns \c true.
   */
  constexpr static_string() noexcept = default;

  /**
   * @brief Constructs from a string literal, copying all \p N bytes.
   *
   * The literal's length \p N (including the terminator) fixes the template
   * parameter via CTAD.
   *
   * @param str Source string literal of length \p N including its terminator.
   *
   * @pre None.
   * @post \c data holds a copy of \p str including its terminator.
   */
  constexpr static_string(char const (&str)[N]) noexcept {  // NOLINT(hicpp-explicit-conversions)
    std::copy_n(static_cast<char const*>(str), N, data.begin());
  }

  /**
   * @brief The string length excluding the null terminator.
   *
   * @return \c N - 1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return N - 1;
  }

  /**
   * @brief Reports whether the string body is empty.
   *
   * @return \c true when the length excluding the terminator is zero.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return size() == 0;
  }

  /**
   * @brief A view over the string body (without the null terminator).
   *
   * @return A \c std::string_view spanning the first \c N - 1 bytes.
   *
   * @pre None.
   * @post The view is valid as long as this object is alive.
   */
  [[nodiscard]] constexpr auto view() const noexcept -> std::string_view {
    return std::string_view{data.data(), N - 1};
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
  [[nodiscard]] constexpr auto operator[](std::size_t const i) const noexcept -> char {
    assert(i < N && "static_string: index out of range");
    return data[i];
  }

  /**
   * @brief Iterator to the first character of the body.
   *
   * @return Pointer to the first character.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto begin() const noexcept -> char const* {
    return data.data();
  }

  /**
   * @brief Iterator one past the last body character.
   *
   * @return Pointer to the null terminator (one past the body).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> char const* {
    return data.data() + (N - 1);
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
   * \c "abcd"; the result length is \c N + M - 1 (both bodies plus one
   * terminator).
   *
   * @tparam M Buffer size (including terminator) of the right operand.
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return A \c static_string<N + M - 1> holding \p a followed by \p b.
   *
   * @pre None.
   * @post The result body is the two bodies concatenated and null-terminated.
   */
  template <std::size_t M>
  [[nodiscard]] friend constexpr auto operator+(
    static_string const& a, static_string<M> const& b
  ) noexcept -> static_string<N + M - 1> {
    static_string<N + M - 1> out{};
    std::copy_n(a.data.begin(), N - 1, out.data.begin());
    std::copy_n(b.data.begin(), M, out.data.begin() + (N - 1));
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
 * Hashes the body through \c std::hash<std::string_view> so a \c static_string
 * can key an unordered container.
 *
 * @tparam N Buffer size of the static string.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N>
struct std::hash<nexenne::utility::static_string<N>> {
  /**
   * @brief Hashes \p s by hashing its body as a \c string_view.
   *
   * @param s Static string to hash.
   *
   * @return The hash of \c s.view().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto operator()(nexenne::utility::static_string<N> const& s
  ) const noexcept -> std::size_t {
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
   * @param s Static string to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post The body has been written into \p ctx using the inherited spec.
   */
  auto format(nexenne::utility::static_string<N> const& s, std::format_context& ctx) const {
    return std::formatter<std::string_view, char>::format(s.view(), ctx);
  }
};
