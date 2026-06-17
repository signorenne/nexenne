#pragma once

/**
 * @file
 * @brief Debug printing and formatting for the geometry types.
 *
 * Mirrors the layering of \c nexenne::math::format: for every public value type
 * and the \c geometry_error enum it provides
 *   - \c to_string(x) returning a readable representation,
 *   - \c operator<<(std::ostream&, x) for stream output, and
 *   - a \c std::formatter specialization so \c std::format("{}", x) works.
 *
 * Each \c std::formatter inherits \c std::formatter<std::string_view>, so a
 * width or alignment spec applies to the whole rendered string and an empty spec
 * gives the default rendering.
 *
 * Covered: \c aabb, \c circle2, \c sphere3, \c ray, \c segment, \c plane3,
 * \c triangle, \c capsule, \c obb2, \c obb3, \c polygon2, \c convex_hull3,
 * \c frustum3, \c frustum_plane, \c transform2d, \c transform3d, and
 * \c geometry_error. The
 * transient GJK and EPA working aggregates (\c gjk_simplex3, \c gjk_result3,
 * \c epa_result3) are intentionally not formatted: like the standard library's
 * \c *_result aggregates they are inspected through their fields, and the math
 * module likewise leaves its result aggregates unformatted.
 *
 * \c to_string for \c geometry_error lives in error.hpp (it needs no
 * \c \<format\>); this header adds its \c operator<< and \c std::formatter.
 */

#include <concepts>
#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/circle.hpp>
#include <nexenne/geometry/convex_hull.hpp>
#include <nexenne/geometry/error.hpp>
#include <nexenne/geometry/frustum.hpp>
#include <nexenne/geometry/obb.hpp>
#include <nexenne/geometry/plane.hpp>
#include <nexenne/geometry/polygon.hpp>
#include <nexenne/geometry/ray.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/transform.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/concepts.hpp>
#include <nexenne/math/format.hpp>

namespace nexenne::geometry {

/**
 * @brief Debug string of the form \c "aabb(min=..., max=...)".
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param a Box to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] auto to_string(aabb<Value, N> const& a) -> std::string {
  return std::format(
    "aabb(min={}, max={})", nexenne::math::to_string(a.min()), nexenne::math::to_string(a.max())
  );
}

/**
 * @brief Streams an \c aabb.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param os Output stream.
 * @param a Box to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted box has been written to \p os.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
auto operator<<(std::ostream& os, aabb<Value, N> const& a) -> std::ostream& {
  return os << to_string(a);
}

/**
 * @brief Debug string \c "circle2(center=..., r=...)".
 *
 * @tparam Real Component type.
 * @param c Circle to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(circle2<Real> const& c) -> std::string {
  return std::format("circle2(center={}, r={})", nexenne::math::to_string(c.center()), c.radius());
}

/**
 * @brief Streams a \c circle2.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param c Circle to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted circle has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, circle2<Real> const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Debug string \c "sphere3(center=..., r=...)".
 *
 * @tparam Real Component type.
 * @param s Sphere to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(sphere3<Real> const& s) -> std::string {
  return std::format("sphere3(center={}, r={})", nexenne::math::to_string(s.center()), s.radius());
}

/**
 * @brief Streams a \c sphere3.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param s Sphere to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted sphere has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, sphere3<Real> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Debug string \c "ray(origin=..., dir=...)".
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param r Ray to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] auto to_string(ray<Real, N> const& r) -> std::string {
  return std::format(
    "ray(origin={}, dir={})",
    nexenne::math::to_string(r.origin()),
    nexenne::math::to_string(r.direction())
  );
}

/**
 * @brief Streams a \c ray.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param os Output stream.
 * @param r Ray to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted ray has been written to \p os.
 */
template <std::floating_point Real, std::size_t N>
auto operator<<(std::ostream& os, ray<Real, N> const& r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief Debug string \c "segment(start=..., end=...)".
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param s Segment to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] auto to_string(segment<Real, N> const& s) -> std::string {
  return std::format(
    "segment(start={}, end={})",
    nexenne::math::to_string(s.start()),
    nexenne::math::to_string(s.end())
  );
}

/**
 * @brief Streams a \c segment.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param os Output stream.
 * @param s Segment to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted segment has been written to \p os.
 */
template <std::floating_point Real, std::size_t N>
auto operator<<(std::ostream& os, segment<Real, N> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Debug string \c "plane3(n=..., d=...)".
 *
 * @tparam Real Component type.
 * @param p Plane to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(plane3<Real> const& p) -> std::string {
  return std::format("plane3(n={}, d={})", nexenne::math::to_string(p.normal()), p.d());
}

/**
 * @brief Streams a \c plane3.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param p Plane to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted plane has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, plane3<Real> const& p) -> std::ostream& {
  return os << to_string(p);
}

/**
 * @brief Debug string \c "triangle(a=..., b=..., c=...)".
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param t Triangle to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] auto to_string(triangle<Real, N> const& t) -> std::string {
  return std::format(
    "triangle(a={}, b={}, c={})",
    nexenne::math::to_string(t.a()),
    nexenne::math::to_string(t.b()),
    nexenne::math::to_string(t.c())
  );
}

/**
 * @brief Streams a \c triangle.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param os Output stream.
 * @param t Triangle to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted triangle has been written to \p os.
 */
template <std::floating_point Real, std::size_t N>
auto operator<<(std::ostream& os, triangle<Real, N> const& t) -> std::ostream& {
  return os << to_string(t);
}

/**
 * @brief Debug string \c "capsule(start=..., end=..., r=...)".
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param c Capsule to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] auto to_string(capsule<Real, N> const& c) -> std::string {
  return std::format(
    "capsule(start={}, end={}, r={})",
    nexenne::math::to_string(c.start()),
    nexenne::math::to_string(c.end()),
    c.radius()
  );
}

/**
 * @brief Streams a \c capsule.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param os Output stream.
 * @param c Capsule to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted capsule has been written to \p os.
 */
template <std::floating_point Real, std::size_t N>
auto operator<<(std::ostream& os, capsule<Real, N> const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Debug string \c "obb2(center=..., half=..., rot=...rad)".
 *
 * @tparam Real Component type.
 * @param o Oriented box to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(obb2<Real> const& o) -> std::string {
  return std::format(
    "obb2(center={}, half={}, rot={}rad)",
    nexenne::math::to_string(o.center()),
    nexenne::math::to_string(o.half_size()),
    o.rotation().value()
  );
}

/**
 * @brief Streams an \c obb2.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param o Oriented box to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted box has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, obb2<Real> const& o) -> std::ostream& {
  return os << to_string(o);
}

/**
 * @brief Debug string \c "obb3(center=..., half=..., rot=quaternion(...))".
 *
 * @tparam Real Component type.
 * @param o Oriented box to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(obb3<Real> const& o) -> std::string {
  return std::format(
    "obb3(center={}, half={}, rot={})",
    nexenne::math::to_string(o.center()),
    nexenne::math::to_string(o.half_size()),
    nexenne::math::to_string(o.rotation())
  );
}

/**
 * @brief Streams an \c obb3.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param o Oriented box to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted box has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, obb3<Real> const& o) -> std::ostream& {
  return os << to_string(o);
}

/**
 * @brief Debug string \c "polygon2(n vertices: v0, v1, ...)".
 *
 * @tparam Real Component type.
 * @param p Polygon to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(polygon2<Real> const& p) -> std::string {
  auto result{std::format("polygon2({} vertices", p.vertices().size())};
  if (!p.vertices().empty()) {
    result += ": ";
    for (auto i{std::size_t{0}}; i < p.vertices().size(); ++i) {
      if (i != 0) {
        result += ", ";
      }
      result += nexenne::math::to_string(p.vertices()[i]);
    }
  }
  result += ')';
  return result;
}

/**
 * @brief Streams a \c polygon2.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param p Polygon to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted polygon has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, polygon2<Real> const& p) -> std::ostream& {
  return os << to_string(p);
}

/**
 * @brief Debug string \c "convex_hull3(n vertices: v0, v1, ...)".
 *
 * @tparam Real Component type.
 * @param h Hull to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(convex_hull3<Real> const& h) -> std::string {
  auto result{std::format("convex_hull3({} vertices", h.vertices().size())};
  if (!h.vertices().empty()) {
    result += ": ";
    for (auto i{std::size_t{0}}; i < h.vertices().size(); ++i) {
      if (i != 0) {
        result += ", ";
      }
      result += nexenne::math::to_string(h.vertices()[i]);
    }
  }
  result += ')';
  return result;
}

/**
 * @brief Streams a \c convex_hull3.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param h Hull to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted hull has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, convex_hull3<Real> const& h) -> std::ostream& {
  return os << to_string(h);
}

/**
 * @brief Debug string \c "frustum3(p0, p1, ..., p5)" listing the six planes.
 *
 * @tparam Real Component type.
 * @param f Frustum to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(frustum3<Real> const& f) -> std::string {
  auto result{std::string{"frustum3("}};
  for (auto i{std::size_t{0}}; i < f.planes().size(); ++i) {
    if (i != 0) {
      result += ", ";
    }
    result += to_string(f.planes()[i]);
  }
  result += ')';
  return result;
}

/**
 * @brief Streams a \c frustum3.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param f Frustum to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted frustum has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, frustum3<Real> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Debug string \c "transform2d(pos=..., rot=...rad, scale=...)".
 *
 * @tparam Real Component type.
 * @param t Pose to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(transform2d<Real> const& t) -> std::string {
  return std::format(
    "transform2d(pos={}, rot={}rad, scale={})",
    nexenne::math::to_string(t.position()),
    t.rotation().value(),
    nexenne::math::to_string(t.scale())
  );
}

/**
 * @brief Streams a \c transform2d.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param t Pose to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted pose has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, transform2d<Real> const& t) -> std::ostream& {
  return os << to_string(t);
}

/**
 * @brief Debug string \c "transform3d(pos=..., rot=quaternion(...), scale=...)".
 *
 * @tparam Real Component type.
 * @param t Pose to print.
 *
 * @return The debug string.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_string(transform3d<Real> const& t) -> std::string {
  return std::format(
    "transform3d(pos={}, rot={}, scale={})",
    nexenne::math::to_string(t.position()),
    nexenne::math::to_string(t.rotation()),
    nexenne::math::to_string(t.scale())
  );
}

/**
 * @brief Streams a \c transform3d.
 *
 * @tparam Real Component type.
 * @param os Output stream.
 * @param t Pose to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted pose has been written to \p os.
 */
template <std::floating_point Real>
auto operator<<(std::ostream& os, transform3d<Real> const& t) -> std::ostream& {
  return os << to_string(t);
}

/**
 * @brief Streams a \c geometry_error by its \c to_string name.
 *
 * @param os Output stream.
 * @param err Error to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The error name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, geometry_error const err) -> std::ostream& {
  return os << to_string(err);
}

/**
 * @brief Streams a \c frustum_plane by its \c to_string name.
 *
 * @param os Output stream.
 * @param which Plane index to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The plane name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, frustum_plane const which) -> std::ostream& {
  return os << to_string(which);
}

}  // namespace nexenne::geometry

/**
 * @brief \c std::format support for \c aabb.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
struct std::formatter<nexenne::geometry::aabb<Value, N>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the box's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param a Box to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted box has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::aabb<Value, N> const& a, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(a), ctx);
  }
};

/**
 * @brief \c std::format support for \c circle2.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::circle2<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the circle's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Circle to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted circle has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::circle2<Real> const& c, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(c), ctx);
  }
};

/**
 * @brief \c std::format support for \c sphere3.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::sphere3<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the sphere's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sphere to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted sphere has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::sphere3<Real> const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c ray.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
struct std::formatter<nexenne::geometry::ray<Real, N>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the ray's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Ray to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted ray has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::ray<Real, N> const& r, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(r), ctx);
  }
};

/**
 * @brief \c std::format support for \c segment.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
struct std::formatter<nexenne::geometry::segment<Real, N>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the segment's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Segment to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted segment has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::segment<Real, N> const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c plane3.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::plane3<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the plane's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param p Plane to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted plane has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::plane3<Real> const& p, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(p), ctx);
  }
};

/**
 * @brief \c std::format support for \c triangle.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
struct std::formatter<nexenne::geometry::triangle<Real, N>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the triangle's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param t Triangle to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted triangle has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::triangle<Real, N> const& t, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(t), ctx);
  }
};

/**
 * @brief \c std::format support for \c capsule.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
struct std::formatter<nexenne::geometry::capsule<Real, N>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the capsule's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Capsule to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted capsule has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::capsule<Real, N> const& c, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(c), ctx);
  }
};

/**
 * @brief \c std::format support for \c obb2.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::obb2<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the box's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param o Box to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted box has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::obb2<Real> const& o, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(o), ctx);
  }
};

/**
 * @brief \c std::format support for \c obb3.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::obb3<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the box's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param o Box to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted box has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::obb3<Real> const& o, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(o), ctx);
  }
};

/**
 * @brief \c std::format support for \c polygon2.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::polygon2<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the polygon's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param p Polygon to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted polygon has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::polygon2<Real> const& p, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(p), ctx);
  }
};

/**
 * @brief \c std::format support for \c convex_hull3.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::convex_hull3<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the hull's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param h Hull to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted hull has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::convex_hull3<Real> const& h, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(h), ctx);
  }
};

/**
 * @brief \c std::format support for \c frustum3.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::frustum3<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the frustum's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param f Frustum to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted frustum has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::frustum3<Real> const& f, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(f), ctx);
  }
};

/**
 * @brief \c std::format support for \c transform2d.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::transform2d<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the pose's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param t Pose to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted pose has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::transform2d<Real> const& t, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(t), ctx);
  }
};

/**
 * @brief \c std::format support for \c transform3d.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct std::formatter<nexenne::geometry::transform3d<Real>> : std::formatter<std::string_view> {
  /**
   * @brief Writes the pose's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param t Pose to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted pose has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::transform3d<Real> const& t, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(t), ctx);
  }
};

/**
 * @brief \c std::format support for \c geometry_error: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name
 * and \c std::format("{}", err) works directly on the error a \c result<T>
 * carries.
 */
template <>
struct std::formatter<nexenne::geometry::geometry_error> : std::formatter<std::string_view> {
  /**
   * @brief Writes the error's \c to_string name through the string formatter.
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
  auto format(nexenne::geometry::geometry_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(err), ctx);
  }
};

/**
 * @brief \c std::format support for \c frustum_plane: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name
 * and \c std::format("{}", which) works directly on a plane index.
 */
template <>
struct std::formatter<nexenne::geometry::frustum_plane> : std::formatter<std::string_view> {
  /**
   * @brief Writes the plane's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param which Plane index to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The plane name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::geometry::frustum_plane const which, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::geometry::to_string(which), ctx);
  }
};
