#pragma once

/**
 * @file
 * @brief Format \c std::source_location into a fixed-size buffer, no allocation.
 *
 * The standard library exposes \c file_name(), \c line(), \c column(), and
 * \c function_name() but no formatter. Loggers usually want a short tag like
 * "file.cpp:42" or a longer "file.cpp:42 in function"; both helpers here write
 * into a caller-supplied buffer with a bounded copy and return a \c string_view
 * into it. The buffer must outlive the view.
 *
 * \code
 * std::array<char, 256> buf{};
 * auto tag{format_short(std::source_location::current(), buf)};
 * log("[" + std::string{tag} + "] starting up");
 * \endcode
 */

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <source_location>
#include <string_view>

namespace nexenne::utility {

/// @cond INTERNAL
namespace detail {

/**
 * @brief Returns the basename of \p path, stripping any leading directories.
 *
 * Scans for the last \c '/' or \c '\\' separator and returns the substring
 * after it, so a full source path collapses to just the file name; a path with
 * no separator is returned unchanged.
 *
 * @param path Path to reduce to its final component.
 *
 * @return A view of \p path from the last separator onward, or all of \p path
 *         when it has no separator.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto basename_of(std::string_view const path) noexcept -> std::string_view {
  auto const pos{path.find_last_of("/\\")};
  if (pos == std::string_view::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

/**
 * @brief Appends as much of \p text as still fits into \p buf, advancing \p pos.
 *
 * \p cap is the usable length: callers pass the buffer size minus one, so a
 * trailing slot always stays free to null-terminate (which also makes a one-byte
 * buffer yield an empty result). The copy is bounded, so it never overruns and
 * the output is always a prefix of \p text.
 *
 * @tparam N Size of the destination buffer.
 * @param buf Destination buffer, written in place.
 * @param pos Current write offset, advanced by the number of bytes copied.
 * @param cap Usable length: the buffer size minus one.
 * @param text Text to append.
 *
 * @pre \p pos is not greater than \p cap.
 * @post \p buf holds the appended prefix and \p pos has advanced past it.
 */
template <std::size_t N>
auto append(
  std::array<char, N>& buf, std::size_t& pos, std::size_t const cap, std::string_view const text
) noexcept -> void {
  auto const n{std::min(text.size(), cap - pos)};
  std::copy_n(text.data(), n, buf.data() + pos);
  pos += n;
}

/**
 * @brief Appends the decimal digits of \p value to \p buf, advancing \p pos.
 *
 * Formats \p value with \c std::to_chars into a scratch buffer sized for the
 * widest 32-bit value (ten digits) and appends the result through \c append, so
 * the write stays bounded by \p cap.
 *
 * @tparam N Size of the destination buffer.
 * @param buf Destination buffer, written in place.
 * @param pos Current write offset, advanced by the number of digits copied.
 * @param cap Usable length: the buffer size minus one.
 * @param value Line number to format in decimal.
 *
 * @pre \p pos is not greater than \p cap.
 * @post \p buf holds the appended digits and \p pos has advanced past them.
 */
template <std::size_t N>
auto append_line(
  std::array<char, N>& buf, std::size_t& pos, std::size_t const cap, unsigned const value
) noexcept -> void {
  auto digits{std::array<char, 10>{}};
  auto const end{std::to_chars(digits.data(), digits.data() + digits.size(), value).ptr};
  append(
    buf, pos, cap, std::string_view{digits.data(), static_cast<std::size_t>(end - digits.data())}
  );
}

}  // namespace detail

/// @endcond

/**
 * @brief Formats a source location as "file.cpp:42" into \p buf.
 *
 * The file path is reduced to its basename so the output is short and
 * grep-friendly. The returned view points into \p buf, which must outlive it,
 * and is truncated to fit when \p buf is too small.
 *
 * @tparam N Size of the caller-supplied buffer.
 * @param loc Source location to format.
 * @param buf Buffer the text is written into; must outlive the result.
 *
 * @return A view over \p buf holding "basename:line", or an empty view when
 *         \p buf is too small to write anything.
 *
 * @pre None.
 * @post \p buf holds the formatted text; the result views \p buf.
 */
template <std::size_t N>
  requires(N >= 1)
[[nodiscard]] auto format_short(std::source_location const& loc, std::array<char, N>& buf) noexcept
  -> std::string_view {
  auto const file{detail::basename_of(loc.file_name())};
  auto pos{std::size_t{0}};
  auto const cap{buf.size() - 1};  // leave one slot to null-terminate buf
  detail::append(buf, pos, cap, file);
  detail::append(buf, pos, cap, ":");
  detail::append_line(buf, pos, cap, loc.line());
  buf[pos] = '\0';  // pos <= cap < N, so this is always in range
  return std::string_view{buf.data(), pos};
}

/**
 * @brief Formats a source location as "file.cpp:42 in function" into \p buf.
 *
 * Like \c format_short but also appends the enclosing function name. The
 * returned view points into \p buf, which must outlive it, and is truncated to
 * fit when \p buf is too small.
 *
 * @tparam N Size of the caller-supplied buffer.
 * @param loc Source location to format.
 * @param buf Buffer the text is written into; must outlive the result.
 *
 * @return A view over \p buf holding "basename:line in function", or an empty
 *         view when \p buf is too small to write anything.
 *
 * @pre None.
 * @post \p buf holds the formatted text; the result views \p buf.
 */
template <std::size_t N>
  requires(N >= 1)
[[nodiscard]] auto format_long(std::source_location const& loc, std::array<char, N>& buf) noexcept
  -> std::string_view {
  auto const file{detail::basename_of(loc.file_name())};
  auto pos{std::size_t{0}};
  auto const cap{buf.size() - 1};  // leave one slot to null-terminate buf
  detail::append(buf, pos, cap, file);
  detail::append(buf, pos, cap, ":");
  detail::append_line(buf, pos, cap, loc.line());
  detail::append(buf, pos, cap, " in ");
  detail::append(buf, pos, cap, std::string_view{loc.function_name()});
  buf[pos] = '\0';  // pos <= cap < N, so this is always in range
  return std::string_view{buf.data(), pos};
}

}  // namespace nexenne::utility
