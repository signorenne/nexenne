#pragma once

/**
 * @file
 * @brief The three formatting layers for every public algorithm type.
 *
 * Each algorithm error enum already carries a \c to_string; this header wires
 * those names into \c std::format so a value returned from an
 * \c std::expected<T, error> prints directly with \c std::format("{}", err),
 * with width and alignment specs for free, and into \c operator<< for streams.
 * Every other public type gets a \c to_string, an \c operator<< and a
 * \c std::formatter that all print the same one-line text: the graph results,
 * the compile-time specs (\c crc_spec, \c modular_sum_spec, \c base_n_spec,
 * \c codec_alphabet), the streaming contexts (\c crc_ctx, \c fnv1a_ctx,
 * \c xxhash_ctx), the online statistics (\c running_stats, \c histogram,
 * \c ema_stats), the interpolators (\c linear_interpolator, \c cubic_spline),
 * \c aho_corasick and \c lca. Keeping the formatters here (rather than in the
 * type headers) leaves those headers free of \c \<format\>: callers pay for
 * the dependency only when they include this header.
 *
 * Specs print their parameters, contexts their running digest as zero-padded
 * hex of the hash width, statistics their count and summary values,
 * interpolators their knot count and domain, the matcher its pattern and node
 * counts, and the tree index its node count. A computed hash or checksum is a
 * plain integer and already formats through the standard library.
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <expected>
#include <format>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

#include <nexenne/algorithm/checksum/crc.hpp>
#include <nexenne/algorithm/checksum/modular_sum.hpp>
#include <nexenne/algorithm/encoding/alphabet.hpp>
#include <nexenne/algorithm/encoding/base_n.hpp>
#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/algorithm/graph/a_star.hpp>
#include <nexenne/algorithm/graph/connected_components.hpp>
#include <nexenne/algorithm/graph/floyd_warshall.hpp>
#include <nexenne/algorithm/graph/kruskal_mst.hpp>
#include <nexenne/algorithm/graph/lca.hpp>
#include <nexenne/algorithm/graph/tarjan_scc.hpp>
#include <nexenne/algorithm/hash/fnv.hpp>
#include <nexenne/algorithm/hash/xxhash.hpp>
#include <nexenne/algorithm/numerical/interpolation.hpp>
#include <nexenne/algorithm/numerical/numerical_error.hpp>
#include <nexenne/algorithm/numerical/online_stats.hpp>
#include <nexenne/algorithm/string/aho_corasick.hpp>

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
 * @brief Streams a \c codec_error by its name.
 *
 * @param os Output stream.
 * @param err Error to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p err has been written to \p os.
 */
inline auto operator<<(std::ostream& os, codec_error const err) -> std::ostream& {
  return os << to_string(err);
}

/**
 * @brief Streams a \c numerical_error by its name.
 *
 * @param os Output stream.
 * @param err Error to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p err has been written to \p os.
 */
inline auto operator<<(std::ostream& os, numerical_error const err) -> std::ostream& {
  return os << to_string(err);
}

/// @cond INTERNAL

namespace detail {

/**
 * @brief Renders a sequence of formattable values as \c "[a, b, c]".
 *
 * Formats each element with \c std::format individually rather than handing the
 * whole range to a library range formatter, which libstdc++ only ships from
 * version 15 onward; this keeps the debug strings identical across toolchains.
 *
 * @tparam Range Input range whose elements are \c std::format formattable.
 * @param seq Sequence to render.
 *
 * @return The bracketed, comma-space separated form of \p seq.
 *
 * @pre The element type of \p seq is formattable via \c std::format.
 * @post \p seq is not modified.
 */
template <std::ranges::input_range Range>
[[nodiscard]] auto format_sequence(Range const& seq) -> std::string {
  auto out{std::string{"["}};
  auto first{true};
  for (auto const& element : seq) {
    if (!first) {
      out += ", ";
    }
    first = false;
    out += std::format("{}", element);
  }
  out += ']';
  return out;
}

/**
 * @brief Renders an unsigned value as \c 0x and zero-padded lowercase hex.
 *
 * Pads to the hex digit count of a \p bits wide register, so a digest or a CRC
 * parameter always shows its full width.
 *
 * @tparam U Unsigned integral value type.
 * @param value Value to render.
 * @param bits Register width in bits that sets the digit count.
 *
 * @return The prefixed hex form of \p value, at least \c (bits+3)/4 digits long.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral U>
[[nodiscard]] auto format_hex(U const value, std::size_t const bits) -> std::string {
  return std::format("0x{:0{}x}", value, (bits + 3) / 4);
}

/**
 * @brief Renders the domain spanned by interpolation knots as \c "[lo, hi]".
 *
 * @tparam T Floating-point knot type.
 * @param xs Strictly increasing knot abscissae.
 *
 * @return The first and last abscissa as \c "[lo, hi]", or \c "[]" when \p xs
 *         is empty.
 *
 * @pre None.
 * @post \p xs is not modified.
 */
template <std::floating_point T>
[[nodiscard]] auto format_domain(std::span<T const> const xs) -> std::string {
  if (xs.empty()) {
    return std::string{"[]"};
  }
  return std::format("[{}, {}]", xs.front(), xs.back());
}

}  // namespace detail

/// @endcond

/**
 * @brief Readable debug string for an \c a_star_result.
 *
 * Renders as \c "a_star_result(path=[...], cost=...)", the path through the
 * internal sequence helper and the cost to its own formatter.
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
  return std::format("a_star_result(path={}, cost={})", detail::format_sequence(r.path), r.cost);
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
  return std::format(
    "scc_result(labels={}, num_components={})", detail::format_sequence(r.labels), r.num_components
  );
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
  return std::format(
    "components_result(labels={}, num_components={})",
    detail::format_sequence(r.labels),
    r.num_components
  );
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
  return std::format(
    "floyd_warshall_result(n={}, distances={})", r.n, detail::format_sequence(r.distances)
  );
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

/**
 * @brief Readable debug string for a \c crc_spec.
 *
 * Renders as \c crc_spec(...) with \c width, \c poly, \c init, \c ref_in,
 * \c ref_out and \c xor_out, the register values as zero-padded hex of the
 * spec width.
 *
 * @tparam WidthBits Register width in bits.
 * @param spec Spec to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p spec is not modified.
 */
template <std::size_t WidthBits>
  requires(WidthBits >= 1 && WidthBits <= 64)
[[nodiscard]] auto to_string(crc_spec<WidthBits> const& spec) -> std::string {
  return std::format(
    "crc_spec(width={}, poly={}, init={}, ref_in={}, ref_out={}, xor_out={})",
    WidthBits,
    detail::format_hex(spec.poly, WidthBits),
    detail::format_hex(spec.init, WidthBits),
    spec.ref_in,
    spec.ref_out,
    detail::format_hex(spec.xor_out, WidthBits)
  );
}

/**
 * @brief Streams a \c crc_spec onto \p os via \c to_string.
 *
 * @tparam WidthBits Register width in bits.
 * @param os Output stream.
 * @param spec Spec to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p spec is not modified; its debug string has been written to \p os.
 */
template <std::size_t WidthBits>
  requires(WidthBits >= 1 && WidthBits <= 64)
auto operator<<(std::ostream& os, crc_spec<WidthBits> const& spec) -> std::ostream& {
  return os << to_string(spec);
}

/**
 * @brief Readable debug string for a \c modular_sum_spec.
 *
 * Renders as \c modular_sum_spec(...) with \c unit_bytes, \c sum_bits,
 * \c modulus and \c init1, every field in decimal.
 *
 * @param spec Spec to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p spec is not modified.
 */
[[nodiscard]] inline auto to_string(modular_sum_spec const& spec) -> std::string {
  return std::format(
    "modular_sum_spec(unit_bytes={}, sum_bits={}, modulus={}, init1={})",
    spec.unit_bytes,
    spec.sum_bits,
    spec.modulus,
    spec.init1
  );
}

/**
 * @brief Streams a \c modular_sum_spec onto \p os via \c to_string.
 *
 * @param os Output stream.
 * @param spec Spec to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p spec is not modified; its debug string has been written to \p os.
 */
inline auto operator<<(std::ostream& os, modular_sum_spec const& spec) -> std::ostream& {
  return os << to_string(spec);
}

/**
 * @brief Readable debug string for a \c codec_alphabet.
 *
 * Renders as \c codec_alphabet(...) holding the symbols in index order as one
 * double-quoted string; the reverse table is derived from them and not printed.
 *
 * @tparam N Number of symbols in the alphabet.
 * @param alphabet Alphabet to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p alphabet is not modified.
 */
template <std::size_t N>
[[nodiscard]] auto to_string(codec_alphabet<N> const& alphabet) -> std::string {
  return std::format("codec_alphabet(\"{}\")", std::string_view{alphabet.chars.data(), N});
}

/**
 * @brief Streams a \c codec_alphabet onto \p os via \c to_string.
 *
 * @tparam N Number of symbols in the alphabet.
 * @param os Output stream.
 * @param alphabet Alphabet to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p alphabet is not modified; its debug string has been written to
 *       \p os.
 */
template <std::size_t N>
auto operator<<(std::ostream& os, codec_alphabet<N> const& alphabet) -> std::ostream& {
  return os << to_string(alphabet);
}

/**
 * @brief Readable debug string for a \c base_n_spec.
 *
 * Renders as \c base_n_spec(...) with \c symbols, the double-quoted
 * \c alphabet in index order, \c padded, the single-quoted \c pad and
 * \c case_insensitive.
 *
 * @tparam Symbols Number of alphabet symbols.
 * @param spec Spec to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p spec is not modified.
 */
template <std::size_t Symbols>
  requires(std::has_single_bit(Symbols) && Symbols >= 2 && Symbols <= 64)
[[nodiscard]] auto to_string(base_n_spec<Symbols> const& spec) -> std::string {
  return std::format(
    "base_n_spec(symbols={}, alphabet=\"{}\", padded={}, pad='{}', case_insensitive={})",
    Symbols,
    std::string_view{spec.alphabet.chars.data(), Symbols},
    spec.padded,
    spec.pad,
    spec.case_insensitive
  );
}

/**
 * @brief Streams a \c base_n_spec onto \p os via \c to_string.
 *
 * @tparam Symbols Number of alphabet symbols.
 * @param os Output stream.
 * @param spec Spec to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p spec is not modified; its debug string has been written to \p os.
 */
template <std::size_t Symbols>
  requires(std::has_single_bit(Symbols) && Symbols >= 2 && Symbols <= 64)
auto operator<<(std::ostream& os, base_n_spec<Symbols> const& spec) -> std::ostream& {
  return os << to_string(spec);
}

/**
 * @brief Readable debug string for a \c crc_ctx.
 *
 * Renders as \c "crc_ctx(value=0x...)", the CRC of every byte fed so far as
 * zero-padded hex of the spec width.
 *
 * @tparam Spec The CRC algorithm the context runs.
 * @param c Context to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p c is not modified.
 */
template <crc_spec Spec>
[[nodiscard]] auto to_string(crc_ctx<Spec> const& c) -> std::string {
  return std::format("crc_ctx(value={})", detail::format_hex(c.value(), crc_ctx<Spec>::width));
}

/**
 * @brief Streams a \c crc_ctx onto \p os via \c to_string.
 *
 * @tparam Spec The CRC algorithm the context runs.
 * @param os Output stream.
 * @param c Context to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p c is not modified; its debug string has been written to \p os.
 */
template <crc_spec Spec>
auto operator<<(std::ostream& os, crc_ctx<Spec> const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Readable debug string for an \c fnv1a_ctx.
 *
 * Renders as \c "fnv1a_ctx(value=0x...)", the running hash as zero-padded hex
 * of the hash width.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 * @param c Context to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p c is not modified.
 */
template <std::size_t Width>
  requires(Width == 32 || Width == 64)
[[nodiscard]] auto to_string(fnv1a_ctx<Width> const& c) -> std::string {
  return std::format("fnv1a_ctx(value={})", detail::format_hex(c.value(), Width));
}

/**
 * @brief Streams an \c fnv1a_ctx onto \p os via \c to_string.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 * @param os Output stream.
 * @param c Context to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p c is not modified; its debug string has been written to \p os.
 */
template <std::size_t Width>
  requires(Width == 32 || Width == 64)
auto operator<<(std::ostream& os, fnv1a_ctx<Width> const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Readable debug string for an \c xxhash_ctx.
 *
 * Renders as \c "xxhash_ctx(value=0x...)", the digest of every byte fed so far
 * as zero-padded hex of the hash width.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 * @param c Context to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p c is not modified.
 */
template <std::size_t Width>
  requires(Width == 32 || Width == 64)
[[nodiscard]] auto to_string(xxhash_ctx<Width> const& c) -> std::string {
  return std::format("xxhash_ctx(value={})", detail::format_hex(c.value(), Width));
}

/**
 * @brief Streams an \c xxhash_ctx onto \p os via \c to_string.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 * @param os Output stream.
 * @param c Context to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p c is not modified; its debug string has been written to \p os.
 */
template <std::size_t Width>
  requires(Width == 32 || Width == 64)
auto operator<<(std::ostream& os, xxhash_ctx<Width> const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Readable debug string for a \c running_stats.
 *
 * Renders as \c running_stats(...) with \c count, \c mean, the population
 * \c stddev, \c min and \c max.
 *
 * @tparam T Floating-point sample type.
 * @param s Accumulator to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p s is not modified.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(running_stats<T> const& s) -> std::string {
  return std::format(
    "running_stats(count={}, mean={}, stddev={}, min={}, max={})",
    s.count(),
    s.mean(),
    s.stddev(),
    s.min(),
    s.max()
  );
}

/**
 * @brief Streams a \c running_stats onto \p os via \c to_string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream.
 * @param s Accumulator to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p s is not modified; its debug string has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, running_stats<T> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Readable debug string for a \c histogram.
 *
 * Renders as \c histogram(...) with \c total, \c underflow, \c overflow and
 * the in-range \c buckets as a sequence, lowest bucket first.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Number of in-range buckets.
 * @param h Histogram to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p h is not modified.
 */
template <std::floating_point T, std::size_t N>
  requires(N > 0)
[[nodiscard]] auto to_string(histogram<T, N> const& h) -> std::string {
  return std::format(
    "histogram(total={}, underflow={}, overflow={}, buckets={})",
    h.total(),
    h.underflow(),
    h.overflow(),
    detail::format_sequence(h.buckets())
  );
}

/**
 * @brief Streams a \c histogram onto \p os via \c to_string.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Number of in-range buckets.
 * @param os Output stream.
 * @param h Histogram to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p h is not modified; its debug string has been written to \p os.
 */
template <std::floating_point T, std::size_t N>
  requires(N > 0)
auto operator<<(std::ostream& os, histogram<T, N> const& h) -> std::ostream& {
  return os << to_string(h);
}

/**
 * @brief Readable debug string for an \c ema_stats.
 *
 * Renders as \c "ema_stats(mean=..., stddev=...)", the exponentially weighted
 * moving mean and standard deviation.
 *
 * @tparam T Floating-point sample type.
 * @param s Estimator to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p s is not modified.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(ema_stats<T> const& s) -> std::string {
  return std::format("ema_stats(mean={}, stddev={})", s.mean(), s.stddev());
}

/**
 * @brief Streams an \c ema_stats onto \p os via \c to_string.
 *
 * @tparam T Floating-point sample type.
 * @param os Output stream.
 * @param s Estimator to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p s is not modified; its debug string has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, ema_stats<T> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief Readable debug string for a \c linear_interpolator.
 *
 * Renders as \c "linear_interpolator(knots=..., domain=[lo, hi])", the domain
 * being the first and last knot abscissa, or \c "[]" for an empty table.
 *
 * @tparam T Floating-point value type of the knots.
 * @param f Interpolator to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p f is not modified.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(linear_interpolator<T> const& f) -> std::string {
  return std::format(
    "linear_interpolator(knots={}, domain={})", f.size(), detail::format_domain(f.xs())
  );
}

/**
 * @brief Streams a \c linear_interpolator onto \p os via \c to_string.
 *
 * @tparam T Floating-point value type of the knots.
 * @param os Output stream.
 * @param f Interpolator to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p f is not modified; its debug string has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, linear_interpolator<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Readable debug string for a \c cubic_spline.
 *
 * Renders as \c "cubic_spline(knots=..., domain=[lo, hi])", the domain being
 * the first and last knot abscissa, or \c "[]" for an empty spline.
 *
 * @tparam T Floating-point value type of the knots.
 * @param f Spline to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p f is not modified.
 */
template <std::floating_point T>
[[nodiscard]] auto to_string(cubic_spline<T> const& f) -> std::string {
  return std::format("cubic_spline(knots={}, domain={})", f.size(), detail::format_domain(f.xs()));
}

/**
 * @brief Streams a \c cubic_spline onto \p os via \c to_string.
 *
 * @tparam T Floating-point value type of the knots.
 * @param os Output stream.
 * @param f Spline to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p f is not modified; its debug string has been written to \p os.
 */
template <std::floating_point T>
auto operator<<(std::ostream& os, cubic_spline<T> const& f) -> std::ostream& {
  return os << to_string(f);
}

/**
 * @brief Readable debug string for an \c aho_corasick matcher.
 *
 * Renders as \c "aho_corasick(patterns=..., nodes=...)", the pattern count and
 * the trie node (automaton state) count, root included.
 *
 * @param m Matcher to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p m is not modified.
 */
[[nodiscard]] inline auto to_string(aho_corasick const& m) -> std::string {
  return std::format("aho_corasick(patterns={}, nodes={})", m.pattern_count(), m.node_count());
}

/**
 * @brief Streams an \c aho_corasick matcher onto \p os via \c to_string.
 *
 * @param os Output stream.
 * @param m Matcher to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p m is not modified; its debug string has been written to \p os.
 */
inline auto operator<<(std::ostream& os, aho_corasick const& m) -> std::ostream& {
  return os << to_string(m);
}

/**
 * @brief Readable debug string for an \c lca index.
 *
 * Renders as \c "lca(nodes=...)", the node count of the indexed tree.
 *
 * @tparam Node Integer node-id type.
 * @param index Index to render.
 *
 * @return The formatted debug string.
 *
 * @pre None.
 * @post \p index is not modified.
 */
template <std::integral Node>
[[nodiscard]] auto to_string(lca<Node> const& index) -> std::string {
  return std::format("lca(nodes={})", index.size());
}

/**
 * @brief Streams an \c lca index onto \p os via \c to_string.
 *
 * @tparam Node Integer node-id type.
 * @param os Output stream.
 * @param index Index to stream.
 *
 * @return \p os, to allow chaining.
 *
 * @pre None.
 * @post \p index is not modified; its debug string has been written to \p os.
 */
template <std::integral Node>
auto operator<<(std::ostream& os, lca<Node> const& index) -> std::ostream& {
  return os << to_string(index);
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
  auto
  format(nexenne::algorithm::floyd_warshall_result<V, Weight> const& r, FormatContext& ctx) const {
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

/**
 * @brief \c std::format support for \c crc_spec via its \c to_string.
 *
 * @tparam WidthBits Register width in bits.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t WidthBits>
  requires(WidthBits >= 1 && WidthBits <= 64)
struct std::formatter<nexenne::algorithm::crc_spec<WidthBits>> {
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
   * @brief Writes the \c to_string form of \p spec to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param spec Spec to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p spec has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::crc_spec<WidthBits> const& spec, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(spec));
  }
};

/**
 * @brief \c std::format support for \c modular_sum_spec via its \c to_string.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::algorithm::modular_sum_spec> {
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
   * @brief Writes the \c to_string form of \p spec to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param spec Spec to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p spec has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::modular_sum_spec const& spec, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(spec));
  }
};

/**
 * @brief \c std::format support for \c codec_alphabet via its \c to_string.
 *
 * @tparam N Number of symbols in the alphabet.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N>
struct std::formatter<nexenne::algorithm::codec_alphabet<N>> {
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
   * @brief Writes the \c to_string form of \p alphabet to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param alphabet Alphabet to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p alphabet has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::codec_alphabet<N> const& alphabet, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(alphabet));
  }
};

/**
 * @brief \c std::format support for \c base_n_spec via its \c to_string.
 *
 * @tparam Symbols Number of alphabet symbols.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t Symbols>
  requires(std::has_single_bit(Symbols) && Symbols >= 2 && Symbols <= 64)
struct std::formatter<nexenne::algorithm::base_n_spec<Symbols>> {
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
   * @brief Writes the \c to_string form of \p spec to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param spec Spec to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p spec has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::base_n_spec<Symbols> const& spec, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(spec));
  }
};

/**
 * @brief \c std::format support for \c crc_ctx via its \c to_string.
 *
 * @tparam Spec The CRC algorithm the context runs.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::algorithm::crc_spec Spec>
struct std::formatter<nexenne::algorithm::crc_ctx<Spec>> {
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
   * @brief Writes the \c to_string form of \p c to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Context to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p c has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::crc_ctx<Spec> const& c, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(c));
  }
};

/**
 * @brief \c std::format support for \c fnv1a_ctx via its \c to_string.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t Width>
  requires(Width == 32 || Width == 64)
struct std::formatter<nexenne::algorithm::fnv1a_ctx<Width>> {
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
   * @brief Writes the \c to_string form of \p c to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Context to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p c has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::fnv1a_ctx<Width> const& c, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(c));
  }
};

/**
 * @brief \c std::format support for \c xxhash_ctx via its \c to_string.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t Width>
  requires(Width == 32 || Width == 64)
struct std::formatter<nexenne::algorithm::xxhash_ctx<Width>> {
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
   * @brief Writes the \c to_string form of \p c to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Context to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p c has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::xxhash_ctx<Width> const& c, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(c));
  }
};

/**
 * @brief \c std::format support for \c running_stats via its \c to_string.
 *
 * @tparam T Floating-point sample type.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
struct std::formatter<nexenne::algorithm::running_stats<T>> {
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
   * @brief Writes the \c to_string form of \p s to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Accumulator to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p s has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::running_stats<T> const& s, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(s));
  }
};

/**
 * @brief \c std::format support for \c histogram via its \c to_string.
 *
 * @tparam T Floating-point sample type.
 * @tparam N Number of in-range buckets.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T, std::size_t N>
  requires(N > 0)
struct std::formatter<nexenne::algorithm::histogram<T, N>> {
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
   * @brief Writes the \c to_string form of \p h to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param h Histogram to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p h has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::histogram<T, N> const& h, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(h));
  }
};

/**
 * @brief \c std::format support for \c ema_stats via its \c to_string.
 *
 * @tparam T Floating-point sample type.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
struct std::formatter<nexenne::algorithm::ema_stats<T>> {
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
   * @brief Writes the \c to_string form of \p s to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Estimator to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p s has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::ema_stats<T> const& s, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(s));
  }
};

/**
 * @brief \c std::format support for \c linear_interpolator via its \c to_string.
 *
 * @tparam T Floating-point value type of the knots.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
struct std::formatter<nexenne::algorithm::linear_interpolator<T>> {
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
   * @brief Writes the \c to_string form of \p f to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param f Interpolator to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p f has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::linear_interpolator<T> const& f, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c cubic_spline via its \c to_string.
 *
 * @tparam T Floating-point value type of the knots.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
struct std::formatter<nexenne::algorithm::cubic_spline<T>> {
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
   * @brief Writes the \c to_string form of \p f to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param f Spline to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p f has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::cubic_spline<T> const& f, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(f));
  }
};

/**
 * @brief \c std::format support for \c aho_corasick via its \c to_string.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::algorithm::aho_corasick> {
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
   * @brief Writes the \c to_string form of \p m to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param m Matcher to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p m has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::aho_corasick const& m, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(m));
  }
};

/**
 * @brief \c std::format support for \c lca via its \c to_string.
 *
 * @tparam Node Integer node-id type.
 *
 * @pre None.
 * @post None.
 */
template <std::integral Node>
struct std::formatter<nexenne::algorithm::lca<Node>> {
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
   * @brief Writes the \c to_string form of \p index to \p ctx.
   *
   * @tparam FormatContext Deduced output context type.
   * @param index Index to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The debug string of \p index has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::algorithm::lca<Node> const& index, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", nexenne::algorithm::to_string(index));
  }
};
