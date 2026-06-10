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
 * Covered types: \c vector, \c matrix, \c quaternion, \c normalized, \c radians,
 * \c degrees, \c fixed, \c sin_cos, \c axis_angle, \c euler_angles, and the
 * \c math_error and \c euler_order enums. The type name in the output matches the
 * alias convention (\c vector3, \c matrix4), so a printed value reads back as the
 * type that produced it.
 */

#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/concepts.hpp>
#include <nexenne/math/error.hpp>
#include <nexenne/math/euler.hpp>
#include <nexenne/math/fixed.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/trigonometry.hpp>
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
 * @brief Debug string of the form "1.5708 rad".
 *
 * @tparam Real Component type.
 * @param r Angle to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the value and the radian unit.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(radians<Real> const r) -> std::string {
  return std::format("{} rad", r.value());
}

/**
 * @brief Debug string of the form "90 deg".
 *
 * @tparam Real Component type.
 * @param d Angle to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the value and the degree unit.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(degrees<Real> const d) -> std::string {
  return std::format("{} deg", d.value());
}

/**
 * @brief Debug string of the form "qA.B(1.5)".
 *
 * The \c qA.B tag names the Q-format (A non-fraction bits, B fraction bits) so a
 * printed value states its resolution; the parenthesized number is the decimal
 * value.
 *
 * @tparam Storage Signed integer storage type.
 * @tparam FractionBits Number of fractional bits.
 * @param x Fixed-point value to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the Q-format and the decimal value.
 */
template <std::signed_integral Storage, std::size_t FractionBits>
[[nodiscard]] auto to_string(fixed<Storage, FractionBits> const x) -> std::string {
  return std::format("q{}.{}({})", sizeof(Storage) * 8 - FractionBits, FractionBits, x.to_float());
}

/**
 * @brief Debug string of the form "sin_cos(sin, cos)".
 *
 * The two values are printed sine first, then cosine, matching the accessor
 * order.
 *
 * @tparam Real Component type.
 * @param sc Sine/cosine pair to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Lists the sine then the cosine.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(sin_cos<Real> const sc) -> std::string {
  return std::format("sin_cos({}, {})", sc.sin(), sc.cos());
}

/**
 * @brief Debug string of the form "axis_angle(axis=vector3(...), angle=... rad)".
 *
 * @tparam Real Component type.
 * @param a Axis-angle rotation to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Names the rotation axis and the angle.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(axis_angle<Real> const& a) -> std::string {
  return std::format("axis_angle(axis={}, angle={})", to_string(a.axis()), to_string(a.angle()));
}

/**
 * @brief Debug string of the form "euler_angles(x=.., y=.., z=.. rad)".
 *
 * The three angles are in radians; the unit is stated once at the end.
 *
 * @tparam Real Component type.
 * @param angles Euler angle triple to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post Lists the three angles in x, y, z order.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(euler_angles<Real> const& angles) -> std::string {
  return std::format("euler_angles(x={}, y={}, z={} rad)", angles.x(), angles.y(), angles.z());
}

/**
 * @brief Lowercase name of a \c euler_order (for example "xyz").
 *
 * @param order Rotation order to describe.
 *
 * @return A static string view naming the order.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(euler_order const order) noexcept -> std::string_view {
  switch (order) {
    case euler_order::xyz:
      return "xyz";
    case euler_order::xzy:
      return "xzy";
    case euler_order::yxz:
      return "yxz";
    case euler_order::yzx:
      return "yzx";
    case euler_order::zxy:
      return "zxy";
    case euler_order::zyx:
      return "zyx";
  }
  return "unknown";
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

/**
 * @brief Streams a \c radians angle via its debug string.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param r Angle to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual angle has been appended to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, radians<Real> const r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief Streams a \c degrees angle via its debug string.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param d Angle to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual angle has been appended to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, degrees<Real> const d) -> std::ostream& {
  return os << to_string(d);
}

/**
 * @brief Streams a \c fixed value via its debug string.
 *
 * @tparam Storage Signed integer storage type.
 * @tparam FractionBits Number of fractional bits.
 * @param os Output stream.
 * @param x Fixed-point value to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual value has been appended to \p os.
 */
template <std::signed_integral Storage, std::size_t FractionBits>
auto operator<<(std::ostream& os, fixed<Storage, FractionBits> const x) -> std::ostream& {
  return os << to_string(x);
}

/**
 * @brief Streams a \c sin_cos pair via its debug string.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param sc Sine/cosine pair to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual pair has been appended to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, sin_cos<Real> const sc) -> std::ostream& {
  return os << to_string(sc);
}

/**
 * @brief Streams an \c axis_angle via its debug string.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param a Axis-angle rotation to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual rotation has been appended to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, axis_angle<Real> const& a) -> std::ostream& {
  return os << to_string(a);
}

/**
 * @brief Streams a \c euler_angles triple via its debug string.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param angles Euler angle triple to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The textual triple has been appended to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, euler_angles<Real> const& angles) -> std::ostream& {
  return os << to_string(angles);
}

/**
 * @brief Streams a \c euler_order via its \c to_string name.
 *
 * @param os Output stream.
 * @param order Rotation order to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The order name has been appended to \p os.
 */
inline auto operator<<(std::ostream& os, euler_order const order) -> std::ostream& {
  return os << to_string(order);
}

/**
 * @brief Streams a \c math_error via its \c to_string name.
 *
 * @param os Output stream.
 * @param err Error to print.
 *
 * @return Reference to \p os, so calls chain.
 *
 * @pre \p os is in a good state.
 * @post The error name has been appended to \p os.
 */
inline auto operator<<(std::ostream& os, math_error const err) -> std::ostream& {
  return os << to_string(err);
}

}  // namespace nexenne::math

/**
 * @brief \c std::format support for \c vector: the spec applies to each component.
 *
 * Holds a component \c std::formatter and forwards the format spec to it, so
 * \c std::format("{:+.3f}", v) formats every element with \c +.3f - e.g.
 * \c "vector3(+1.000, +2.000, +3.000)". The empty spec gives the default
 * rendering (same as \c to_string).
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
struct std::formatter<nexenne::math::vector<Value, N>> {
  std::formatter<Value> component;  ///< Parses and applies the per-component spec.

  /**
   * @brief Forwards the spec to the component formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes the vector, formatting each component with the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param v Vector to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted vector has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::vector<Value, N> const& v, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "vector{}(", N)};
    for (std::size_t i{0}; i < N; ++i) {
      if (i != 0) {
        out = std::format_to(out, ", ");
      }
      ctx.advance_to(out);
      out = component.format(v[i], ctx);
    }
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c quaternion: the spec applies to x, y, z, w.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::math::quaternion<Real>> {
  std::formatter<Real> component;  ///< Parses and applies the per-component spec.

  /**
   * @brief Forwards the spec to the component formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post The held component formatter holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes the quaternion as \c "quaternion(x, y, z; w)" with the spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param q Quaternion to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted quaternion has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::quaternion<Real> const q, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "quaternion(")};
    ctx.advance_to(out);
    out = component.format(q.x(), ctx);
    out = std::format_to(out, ", ");
    ctx.advance_to(out);
    out = component.format(q.y(), ctx);
    out = std::format_to(out, ", ");
    ctx.advance_to(out);
    out = component.format(q.z(), ctx);
    out = std::format_to(out, "; ");
    ctx.advance_to(out);
    out = component.format(q.w(), ctx);
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c matrix: the spec applies to each element.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
struct std::formatter<nexenne::math::matrix<Value, N>> {
  std::formatter<Value> component;  ///< Parses and applies the per-element spec.

  /**
   * @brief Forwards the spec to the component formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post The held component formatter holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes the matrix in row-major order, each element with the spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param m Matrix to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted matrix has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::matrix<Value, N> const& m, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "matrix{}(", N)};
    for (std::size_t r{0}; r < N; ++r) {
      if (r != 0) {
        out = std::format_to(out, "; ");
      }
      for (std::size_t c{0}; c < N; ++c) {
        if (c != 0) {
          out = std::format_to(out, ", ");
        }
        ctx.advance_to(out);
        out = component.format(m(r, c), ctx);
      }
    }
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c normalized: forwards the spec to the vector.
 *
 * @tparam Real Component type.
 * @tparam N Component count.
 */
template <std::floating_point Real, std::size_t N>
struct std::formatter<nexenne::math::normalized<Real, N>> {
  std::formatter<nexenne::math::vector<Real, N>> inner;  ///< Formats the wrapped vector.

  /**
   * @brief Forwards the spec to the wrapped-vector formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post The held vector formatter holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return inner.parse(ctx);
  }

  /**
   * @brief Writes \c "normalized(...)" around the wrapped vector's formatting.
   *
   * @tparam FormatContext Deduced output context type.
   * @param n Unit-length wrapper to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted wrapper has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::normalized<Real, N> const& n, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "normalized(")};
    ctx.advance_to(out);
    out = inner.format(n.value(), ctx);
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c math_error: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name,
 * and so \c std::format("{}", err) works directly on a value returned from a
 * \c result<T> without a manual \c to_string call.
 */
template <>
struct std::formatter<nexenne::math::math_error> : std::formatter<std::string_view> {
  /**
   * @brief Formats the error's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param err Error to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The error name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::math_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::math::to_string(err), ctx);
  }
};

/**
 * @brief \c std::format support for \c radians: the spec applies to the value.
 *
 * Forwards the format spec to the wrapped scalar and appends the \c " rad" unit,
 * so \c std::format("{:.2f}", r) gives \c "1.57 rad".
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::math::radians<Real>> {
  std::formatter<Real> component;  ///< Parses and applies the value spec.

  /**
   * @brief Forwards the spec to the value formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes the value with the parsed spec, then \c " rad".
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Angle to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted angle has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::radians<Real> const r, FormatContext& ctx) const {
    auto out{component.format(r.value(), ctx)};
    return std::format_to(out, " rad");
  }
};

/**
 * @brief \c std::format support for \c degrees: the spec applies to the value.
 *
 * Forwards the format spec to the wrapped scalar and appends the \c " deg" unit.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::math::degrees<Real>> {
  std::formatter<Real> component;  ///< Parses and applies the value spec.

  /**
   * @brief Forwards the spec to the value formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes the value with the parsed spec, then \c " deg".
   *
   * @tparam FormatContext Deduced output context type.
   * @param d Angle to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted angle has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::degrees<Real> const d, FormatContext& ctx) const {
    auto out{component.format(d.value(), ctx)};
    return std::format_to(out, " deg");
  }
};

/**
 * @brief \c std::format support for \c fixed: the spec applies to the decimal value.
 *
 * Prints the \c "qA.B(" Q-format tag, forwards the spec to the value converted to
 * \c double, then a closing parenthesis.
 *
 * @tparam Storage Signed integer storage type.
 * @tparam FractionBits Number of fractional bits.
 */
template <std::signed_integral Storage, std::size_t FractionBits>
struct std::formatter<nexenne::math::fixed<Storage, FractionBits>> {
  std::formatter<double> component;  ///< Parses and applies the value spec.

  /**
   * @brief Forwards the spec to the value formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes \c "qA.B(value)" with the value formatted by the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param x Fixed-point value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted value has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::fixed<Storage, FractionBits> const x, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "q{}.{}(", sizeof(Storage) * 8 - FractionBits, FractionBits)};
    ctx.advance_to(out);
    out = component.format(x.to_float(), ctx);
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c sin_cos: the spec applies to both fields.
 *
 * Writes \c "sin_cos(sin, cos)" with each value formatted by the parsed spec.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::math::sin_cos<Real>> {
  std::formatter<Real> component;  ///< Parses and applies the per-field spec.

  /**
   * @brief Forwards the spec to the field formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes \c "sin_cos(sin, cos)" with each field under the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param sc Sine/cosine pair to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted pair has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::sin_cos<Real> const sc, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "sin_cos(")};
    ctx.advance_to(out);
    out = component.format(sc.sin(), ctx);
    out = std::format_to(out, ", ");
    ctx.advance_to(out);
    out = component.format(sc.cos(), ctx);
    *out++ = ')';
    return out;
  }
};

/**
 * @brief \c std::format support for \c axis_angle: the spec applies to each scalar.
 *
 * Writes \c "axis_angle(axis=vector3(x, y, z), angle=a rad)" with every scalar
 * (the three axis components and the angle) formatted by the parsed spec.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::math::axis_angle<Real>> {
  std::formatter<Real> component;  ///< Parses and applies the per-scalar spec.

  /**
   * @brief Forwards the spec to the scalar formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes the axis-angle with each scalar under the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param a Axis-angle rotation to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted rotation has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::axis_angle<Real> const& a, FormatContext& ctx) const {
    auto const& axis{a.axis()};
    auto out{std::format_to(ctx.out(), "axis_angle(axis=vector3(")};
    ctx.advance_to(out);
    out = component.format(axis[0], ctx);
    out = std::format_to(out, ", ");
    ctx.advance_to(out);
    out = component.format(axis[1], ctx);
    out = std::format_to(out, ", ");
    ctx.advance_to(out);
    out = component.format(axis[2], ctx);
    out = std::format_to(out, "), angle=");
    ctx.advance_to(out);
    out = component.format(a.angle().value(), ctx);
    return std::format_to(out, " rad)");
  }
};

/**
 * @brief \c std::format support for \c euler_angles: the spec applies to each angle.
 *
 * Writes \c "euler_angles(x=.., y=.., z=.. rad)" with each of the three radian
 * angles formatted by the parsed spec.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::math::euler_angles<Real>> {
  std::formatter<Real> component;  ///< Parses and applies the per-angle spec.

  /**
   * @brief Forwards the spec to the angle formatter.
   *
   * @param ctx Format parse context.
   *
   * @return Iterator past the parsed spec.
   *
   * @pre None.
   * @post \c component holds the parsed spec.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    return component.parse(ctx);
  }

  /**
   * @brief Writes the three angles in x, y, z order under the parsed spec.
   *
   * @tparam FormatContext Deduced output context type.
   * @param e Euler angle triple to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted triple has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::euler_angles<Real> const& e, FormatContext& ctx) const {
    auto out{std::format_to(ctx.out(), "euler_angles(x=")};
    ctx.advance_to(out);
    out = component.format(e.x(), ctx);
    out = std::format_to(out, ", y=");
    ctx.advance_to(out);
    out = component.format(e.y(), ctx);
    out = std::format_to(out, ", z=");
    ctx.advance_to(out);
    out = component.format(e.z(), ctx);
    return std::format_to(out, " rad)");
  }
};

/**
 * @brief \c std::format support for \c euler_order: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name,
 * exactly like the \c math_error formatter.
 */
template <>
struct std::formatter<nexenne::math::euler_order> : std::formatter<std::string_view> {
  /**
   * @brief Formats the order's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param order Rotation order to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The order name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::math::euler_order const order, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::math::to_string(order), ctx);
  }
};
