#pragma once

/**
 * @file
 * @brief Debug printing and formatting for the math value types.
 *
 * Three interlocking layers for every covered type:
 *   - \c to_string(v) producing a readable representation;
 *   - \c operator<<(std::ostream&, v) for stream output (delegates to to_string);
 *   - a \c std::formatter specialization so \c std::format("{}", v) works.
 *
 * Covered types: \c vector, \c matrix, \c quaternion, \c normalized. The type
 * name in the output matches the alias convention (\c vector3, \c matrix4), so a
 * printed value reads back as the type that produced it.
 */

#include <cstddef>
#include <format>
#include <ostream>
#include <string>

#include <nexenne/math/concepts.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>

namespace nexenne::math {

/**
 * @brief Debug string of the form "vectorN(c0, c1, ..., cN-1)".
 *
 * @tparam Value Component type, formattable via \c std::format.
 * @tparam N Component count.
 * @param v Vector to print.
 *
 * @return The debug string.
 *
 * @pre \p Value is formattable via \c std::format.
 * @post Names the dimension and lists every component.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] auto to_string(vector<Value, N> const& v) -> std::string {
  auto result{std::format("vector{}(", N)};
  for (std::size_t i{0}; i < N; ++i) {
    if (i != 0) {
      result += ", ";
    }
    result += std::format("{}", v[i]);
  }
  result += ')';
  return result;
}

/**
 * @brief Debug string of the form "quaternion(x, y, z; w)".
 *
 * The semicolon separates the vector part from the scalar part, so it is obvious
 * at a glance which component is the real part.
 *
 * @tparam Real Component type.
 * @param q Quaternion to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Lists the vector part and the scalar part.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(quaternion<Real> const q) -> std::string {
  return std::format("quaternion({}, {}, {}; {})", q.x(), q.y(), q.z(), q.w());
}

/**
 * @brief Debug string of the form "matrixN(row0; row1; ...; rowN-1)".
 *
 * Rows are printed in natural reading order regardless of the column-major
 * in-memory layout.
 *
 * @tparam Value Component type, formattable via \c std::format.
 * @tparam N Matrix dimension.
 * @param m Matrix to print.
 *
 * @return The debug string.
 *
 * @pre \p Value is formattable via \c std::format.
 * @post Lists every element in row-major reading order.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] auto to_string(matrix<Value, N> const& m) -> std::string {
  auto result{std::format("matrix{}(", N)};
  for (std::size_t r{0}; r < N; ++r) {
    if (r != 0) {
      result += "; ";
    }
    for (std::size_t c{0}; c < N; ++c) {
      if (c != 0) {
        result += ", ";
      }
      result += std::format("{}", m(r, c));
    }
  }
  result += ')';
  return result;
}

/**
 * @brief Debug string of the form "normalized(vectorN(...))".
 *
 * @tparam Real Component type.
 * @tparam N Component count.
 * @param n Unit-length wrapper to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Wraps the underlying vector's debug string.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] auto to_string(normalized<Real, N> const& n) -> std::string {
  return std::format("normalized({})", to_string(n.value()));
}

/**
 * @brief Streams a \c vector via its debug string.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param os Output stream.
 * @param v Vector to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual vector has been appended to \p os.
 */
template <arithmetic Value, std::size_t N>
auto operator<<(std::ostream& os, vector<Value, N> const& v) -> std::ostream& {
  return os << to_string(v);
}

/**
 * @brief Streams a \c quaternion via its debug string.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param q Quaternion to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual quaternion has been appended to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, quaternion<Real> const q) -> std::ostream& {
  return os << to_string(q);
}

/**
 * @brief Streams a \c matrix via its debug string.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param os Output stream.
 * @param m Matrix to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual matrix has been appended to \p os.
 */
template <arithmetic Value, std::size_t N>
auto operator<<(std::ostream& os, matrix<Value, N> const& m) -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Streams a \c normalized via its debug string.
 *
 * @tparam Real Component type.
 * @tparam N Component count.
 * @param os Output stream.
 * @param n Unit-length wrapper to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual wrapper has been appended to \p os.
 */
template <std::floating_point Real, std::size_t N>
auto operator<<(std::ostream& os, normalized<Real, N> const& n) -> std::ostream& {
  return os << to_string(n);
}

}  // namespace nexenne::math

/**
 * @brief \c std::format support for \c vector (empty format spec only).
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
struct std::formatter<nexenne::math::vector<Value, N>> {
  /**
   * @brief Parses the format spec, accepting only the empty one.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator to the first unparsed character.
   *
   * @pre None.
   * @post None.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    if (it != ctx.end() && *it != '}') {
      throw std::format_error("nexenne::math formatters take no format specification");
    }
    return it;
  }

  /**
   * @brief Writes the vector's debug string into the context output.
   *
   * @tparam FormatContext Deduced output context type.
   * @param v Vector to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The textual vector has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::vector<Value, N> const& v, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::math::to_string(v));
  }
};

/**
 * @brief \c std::format support for \c quaternion (empty format spec only).
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::math::quaternion<Real>> {
  /**
   * @brief Parses the format spec, accepting only the empty one.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator to the first unparsed character.
   *
   * @pre None.
   * @post None.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    if (it != ctx.end() && *it != '}') {
      throw std::format_error("nexenne::math formatters take no format specification");
    }
    return it;
  }

  /**
   * @brief Writes the quaternion's debug string into the context output.
   *
   * @tparam FormatContext Deduced output context type.
   * @param q Quaternion to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The textual quaternion has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::quaternion<Real> const q, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::math::to_string(q));
  }
};

/**
 * @brief \c std::format support for \c matrix (empty format spec only).
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
struct std::formatter<nexenne::math::matrix<Value, N>> {
  /**
   * @brief Parses the format spec, accepting only the empty one.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator to the first unparsed character.
   *
   * @pre None.
   * @post None.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    if (it != ctx.end() && *it != '}') {
      throw std::format_error("nexenne::math formatters take no format specification");
    }
    return it;
  }

  /**
   * @brief Writes the matrix's debug string into the context output.
   *
   * @tparam FormatContext Deduced output context type.
   * @param m Matrix to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The textual matrix has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::matrix<Value, N> const& m, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::math::to_string(m));
  }
};

/**
 * @brief \c std::format support for \c normalized (empty format spec only).
 *
 * @tparam Real Component type.
 * @tparam N Component count.
 */
template <std::floating_point Real, std::size_t N>
struct std::formatter<nexenne::math::normalized<Real, N>> {
  /**
   * @brief Parses the format spec, accepting only the empty one.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator to the first unparsed character.
   *
   * @pre None.
   * @post None.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    if (it != ctx.end() && *it != '}') {
      throw std::format_error("nexenne::math formatters take no format specification");
    }
    return it;
  }

  /**
   * @brief Writes the wrapper's debug string into the context output.
   *
   * @tparam FormatContext Deduced output context type.
   * @param n Unit-length wrapper to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The textual wrapper has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::normalized<Real, N> const& n, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::math::to_string(n));
  }
};
