#pragma once

/**
 * @file
 * @brief Opt-in \c std::format support for the algorithm error enums.
 *
 * Each algorithm error enum already carries a \c to_string; this header wires
 * those names into \c std::format so a value returned from an
 * \c std::expected<T, error> prints directly with \c std::format("{}", err),
 * with width and alignment specs for free. Keeping the formatters here (rather
 * than in the core error headers) leaves those headers free of \c \<format\>:
 * callers pay for the dependency only when they include this header.
 *
 * Formatter policy for the algorithm module: only the values that flow out of a
 * codec, checksum, or numerical call and are shown to a user, the error enums,
 * ship a formatter. The compile-time configuration types (\c crc_spec,
 * \c modular_sum_spec, \c base_n_spec, \c codec_alphabet) and the opaque
 * streaming engines (\c crc_ctx, \c fnv1a_ctx, \c xxhash_ctx) are template
 * parameters and internal accumulator state, not printable value types, so they
 * carry no formatter by design. A computed hash or checksum is a plain integer
 * and already formats through the standard library.
 */

#include <concepts>
#include <expected>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/algorithm/graph/a_star.hpp>
#include <nexenne/algorithm/graph/connected_components.hpp>
#include <nexenne/algorithm/graph/floyd_warshall.hpp>
#include <nexenne/algorithm/graph/kruskal_mst.hpp>
#include <nexenne/algorithm/graph/tarjan_scc.hpp>
#include <nexenne/algorithm/numerical/numerical_error.hpp>

/**
 * @brief \c std::format support for \c codec_error: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name,
 * and so \c std::format("{}", err) works directly on a value returned from a
 * \c codec_result without a manual \c to_string call.
 */
template <>
struct std::formatter<nexenne::algorithm::codec_error> : std::formatter<std::string_view> {
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
  auto format(nexenne::algorithm::codec_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::algorithm::to_string(err), ctx);
  }
};

/**
 * @brief \c std::format support for \c numerical_error: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name,
 * and so \c std::format("{}", err) works directly on a value returned from an
 * \c std::expected<T, numerical_error> without a manual \c to_string call.
 */
template <>
struct std::formatter<nexenne::algorithm::numerical_error> : std::formatter<std::string_view> {
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
  auto format(nexenne::algorithm::numerical_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::algorithm::to_string(err), ctx);
  }
};

namespace nexenne::algorithm {

/**
 * @brief Readable debug string for an \c a_star_result.
 *
 * Renders as \c "a_star_result(path=[...], cost=...)", the path forwarding to
 * the standard sequence formatter and the cost to its own.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric cost type.
 * @param r Result to render.
 *
 * @return The formatted debug string.
 *
 * @pre \c V and \c Weight are formattable via \c std::format.
 * @post \p r is not modified.
 */
template <std::unsigned_integral V, typename Weight>
[[nodiscard]] auto to_string(a_star_result<V, Weight> const& r) -> std::string {
  return std::format("a_star_result(path={}, cost={})", r.path, r.cost);
}

/**
 * @brief Streams an \c a_star_result onto \p os via \c to_string.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric cost type.
 * @param os Output stream.
 * @param r Result to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p r is not modified; its debug string has been written to \p os.
 */
template <std::unsigned_integral V, typename Weight>
auto operator<<(std::ostream& os, a_star_result<V, Weight> const& r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief Readable debug string for an \c scc_result.
 *
 * Renders as \c "scc_result(labels=[...], num_components=...)".
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @param r Result to render.
 *
 * @return The formatted debug string.
 *
 * @pre \c V is formattable via \c std::format.
 * @post \p r is not modified.
 */
template <std::unsigned_integral V>
[[nodiscard]] auto to_string(scc_result<V> const& r) -> std::string {
  return std::format("scc_result(labels={}, num_components={})", r.labels, r.num_components);
}

/**
 * @brief Streams an \c scc_result onto \p os via \c to_string.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @param os Output stream.
 * @param r Result to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p r is not modified; its debug string has been written to \p os.
 */
template <std::unsigned_integral V>
auto operator<<(std::ostream& os, scc_result<V> const& r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief Readable debug string for a \c components_result.
 *
 * Renders as \c "components_result(labels=[...], num_components=...)".
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 * @param r Result to render.
 *
 * @return The formatted debug string.
 *
 * @pre \c V is formattable via \c std::format.
 * @post \p r is not modified.
 */
template <typename E, std::unsigned_integral V>
[[nodiscard]] auto to_string(components_result<E, V> const& r) -> std::string {
  return std::format("components_result(labels={}, num_components={})", r.labels, r.num_components);
}

/**
 * @brief Streams a \c components_result onto \p os via \c to_string.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 * @param os Output stream.
 * @param r Result to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p r is not modified; its debug string has been written to \p os.
 */
template <typename E, std::unsigned_integral V>
auto operator<<(std::ostream& os, components_result<E, V> const& r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief Readable debug string for a \c floyd_warshall_result.
 *
 * Renders as \c "floyd_warshall_result(n=..., distances=[...])", the distances
 * being the flat row-major matrix in reading order.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric distance type.
 * @param r Result to render.
 *
 * @return The formatted debug string.
 *
 * @pre \c Weight is formattable via \c std::format.
 * @post \p r is not modified.
 */
template <std::unsigned_integral V, typename Weight>
[[nodiscard]] auto to_string(floyd_warshall_result<V, Weight> const& r) -> std::string {
  return std::format("floyd_warshall_result(n={}, distances={})", r.n, r.distances);
}

/**
 * @brief Streams a \c floyd_warshall_result onto \p os via \c to_string.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric distance type.
 * @param os Output stream.
 * @param r Result to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p r is not modified; its debug string has been written to \p os.
 */
template <std::unsigned_integral V, typename Weight>
auto operator<<(std::ostream& os, floyd_warshall_result<V, Weight> const& r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief Readable debug string for an \c mst_edge.
 *
 * Renders as \c "mst_edge(from=..., to=..., weight=...)".
 *
 * @tparam E Edge weight type.
 * @tparam V Unsigned-integer vertex ID type.
 * @param e Edge to render.
 *
 * @return The formatted debug string.
 *
 * @pre \c V and \c E are formattable via \c std::format.
 * @post \p e is not modified.
 */
template <typename E, std::unsigned_integral V>
[[nodiscard]] auto to_string(mst_edge<E, V> const& e) -> std::string {
  return std::format("mst_edge(from={}, to={}, weight={})", e.from, e.to, e.weight);
}

/**
 * @brief Streams an \c mst_edge onto \p os via \c to_string.
 *
 * @tparam E Edge weight type.
 * @tparam V Unsigned-integer vertex ID type.
 * @param os Output stream.
 * @param e Edge to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p e is not modified; its debug string has been written to \p os.
 */
template <typename E, std::unsigned_integral V>
auto operator<<(std::ostream& os, mst_edge<E, V> const& e) -> std::ostream& {
  return os << to_string(e);
}

}  // namespace nexenne::algorithm

/**
 * @brief \c std::format support for \c a_star_result via its \c to_string.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric cost type.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral V, typename Weight>
struct std::formatter<nexenne::algorithm::a_star_result<V, Weight>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @tparam ParseContext Deduced parse-context type.
   * @param ctx Parse context.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p r to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Result to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p r has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::a_star_result<V, Weight> const& r, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(r));
  }
};

/**
 * @brief \c std::format support for \c scc_result via its \c to_string.
 *
 * @tparam V Unsigned-integer vertex ID type.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral V>
struct std::formatter<nexenne::algorithm::scc_result<V>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @tparam ParseContext Deduced parse-context type.
   * @param ctx Parse context.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p r to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Result to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p r has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::scc_result<V> const& r, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(r));
  }
};

/**
 * @brief \c std::format support for \c components_result via its \c to_string.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 *
 * @pre None.
 * @post None.
 */
template <typename E, std::unsigned_integral V>
struct std::formatter<nexenne::algorithm::components_result<E, V>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @tparam ParseContext Deduced parse-context type.
   * @param ctx Parse context.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p r to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Result to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p r has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::components_result<E, V> const& r, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(r));
  }
};

/**
 * @brief \c std::format support for \c floyd_warshall_result via its \c to_string.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric distance type.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral V, typename Weight>
struct std::formatter<nexenne::algorithm::floyd_warshall_result<V, Weight>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @tparam ParseContext Deduced parse-context type.
   * @param ctx Parse context.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p r to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Result to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p r has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::floyd_warshall_result<V, Weight> const& r, FormatContext& ctx)
    const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(r));
  }
};

/**
 * @brief \c std::format support for \c mst_edge via its \c to_string.
 *
 * @tparam E Edge weight type.
 * @tparam V Unsigned-integer vertex ID type.
 *
 * @pre None.
 * @post None.
 */
template <typename E, std::unsigned_integral V>
struct std::formatter<nexenne::algorithm::mst_edge<E, V>> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @tparam ParseContext Deduced parse-context type.
   * @param ctx Parse context.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre None.
   * @post None.
   */
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the \c to_string form of \p e to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param e Edge to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p e has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::mst_edge<E, V> const& e, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(e));
  }
};
