#pragma once

/**
 * @file
 * @brief Format \c std::source_location into a fixed-size buffer, no allocation.
 *
 * The standard library exposes \c file_name(), \c line(), \c column(), and
 * \c function_name() but no formatter. Loggers usually want a short tag like
 * "file.cpp:42" or a longer "file.cpp:42 in function"; both helpers here write
 * into a caller-supplied buffer with \c snprintf and return a \c string_view
 * into it. The buffer must outlive the view.
 *
 * \code
 * std::array<char, 256> buf{};
 * auto tag{format_short(std::source_location::current(), buf)};
 * log("[" + std::string{tag} + "] starting up");
 * \endcode
 */

#include <array>
#include <cstddef>
#include <cstdio>
#include <source_location>
#include <string_view>

namespace nexenne::utility {

/// @cond INTERNAL
namespace detail {

// Strips leading path components, returning the basename.
[[nodiscard]] constexpr auto basename_of(std::string_view const path) noexcept -> std::string_view {
  auto const pos{path.find_last_of("/\\")};
  if (pos == std::string_view::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

// Turns an snprintf return value into a string_view over buf, handling the
// error (<= 0) and truncation (>= buffer size) cases: snprintf returns the
// length it WOULD have written, so a value at or past the buffer size means the
// output was clipped to size - 1 usable chars.
template <std::size_t N>
[[nodiscard]] auto
view_of_snprintf(std::array<char, N> const& buf, int const written) noexcept -> std::string_view {
  if (written <= 0) {
    return std::string_view{};
  }
  auto const n{
    static_cast<std::size_t>(written) >= buf.size() ? buf.size() - 1
                                                    : static_cast<std::size_t>(written)
  };
  return std::string_view{buf.data(), n};
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
  auto const written{std::snprintf(
    buf.data(), buf.size(), "%.*s:%u", static_cast<int>(file.size()), file.data(), loc.line()
  )};
  return detail::view_of_snprintf(buf, written);
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
  auto const* const func{loc.function_name()};
  auto const written{std::snprintf(
    buf.data(),
    buf.size(),
    "%.*s:%u in %s",
    static_cast<int>(file.size()),
    file.data(),
    loc.line(),
    func
  )};
  return detail::view_of_snprintf(buf, written);
}

}  // namespace nexenne::utility
