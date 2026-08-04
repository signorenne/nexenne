#pragma once

/**
 * @file
 * @brief JSON serialiser, DOM \c value to a \c std::string.
 *
 * Two modes:
 *
 *   - \c serialize(v)         : compact one-line output, no whitespace.
 *   - \c serialize_pretty(v)  : human-readable indented output.
 *
 * Keys are emitted in the sorted \c flat_map iteration order, so the
 * output is deterministic and stable for diffing. Numbers use
 * \c std::to_chars for round-trippable representations. Strings escape
 * the JSON-required set (the quote, the backslash, and the control
 * characters below 0x20). String bytes at or above 0x20 are copied
 * through unvalidated: the serialiser does not check that a string is
 * well-formed UTF-8, so that is the caller's responsibility. The one
 * exception is \c serialize_options::ascii_only, which decodes each
 * UTF-8 sequence and substitutes U+FFFD for any malformed, overlong,
 * surrogate, or out-of-range one, keeping that output valid JSON.
 */

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <nexenne/serialization/json/value.hpp>

namespace nexenne::serialization::json {

/**
 * @brief Formatting options shared by \c serialize and
 *        \c serialize_pretty.
 */
struct serialize_options {
  std::size_t indent{2};   ///< Spaces per nesting level (pretty output only).
  bool ascii_only{false};  ///< Escape every non-ASCII byte as a \\uXXXX sequence.
};

namespace detail {

/// @cond INTERNAL

/**
 * @brief Append a \c \\uXXXX escape for a 16-bit code unit to \p out.
 *
 * Uses manual hex so no \c snprintf or locale is pulled in (faster, and
 * freestanding-friendly).
 *
 * @param out Output string the escape is appended to.
 * @param code The 16-bit code unit to escape.
 *
 * @pre None.
 * @post \p out has grown by the six characters of the escape.
 */
auto append_u_escape(std::string& out, std::uint16_t const code) -> void;

/**
 * @brief Append \p s to \p out as a quoted, JSON-escaped string.
 *
 * Escapes the JSON-required set (the quote, the backslash, and control bytes
 * below 0x20). With \p ascii_only set, each non-ASCII UTF-8 sequence is
 * decoded and re-emitted as a \c \\uXXXX escape (a surrogate pair above the
 * BMP), and any malformed, overlong, surrogate, or out-of-range sequence
 * becomes U+FFFD so the output stays valid JSON; otherwise bytes at or above
 * 0x20 are copied through unvalidated.
 *
 * @param out Output string the quoted literal is appended to.
 * @param s Characters to escape and emit.
 * @param ascii_only When \c true, escape every non-ASCII byte.
 *
 * @pre None.
 * @post \p out has grown by the quoted, escaped form of \p s.
 */
auto write_escaped_string(std::string& out, std::string_view const s, bool const ascii_only)
  -> void;

/**
 * @brief Append the JSON rendering of a double \p d to \p out.
 *
 * Renders with \c std::to_chars; NaN and infinity become \c null since JSON
 * cannot represent them. A magnitude that \c std::to_chars prints in pure
 * integer form gets a trailing \c ".0" so it reparses as a float rather than
 * overflowing an integer.
 *
 * @param out Output string the number is appended to.
 * @param d Value to render.
 *
 * @pre None.
 * @post \p out has grown by the rendered number (or \c null).
 */
auto write_number(std::string& out, double const d) -> void;

/**
 * @brief Append the decimal rendering of a signed integer \p i to \p out.
 *
 * @param out Output string the number is appended to.
 * @param i Value to render.
 *
 * @pre None.
 * @post \p out has grown by the rendered integer.
 */
auto write_number(std::string& out, std::int64_t const i) -> void;

/// @endcond

/**
 * @brief Kind of a pending unit of output on the serialisation work stack.
 *
 * The serialiser walks the DOM with an explicit stack rather than recursion so
 * a hand-built DOM deeper than any call stack (the value type supports one,
 * matching its iterative destructor and \c operator==) serialises without
 * overflowing.
 */
enum class emit_op : std::uint8_t {
  render,   ///< Render \c node at \c depth, expanding a container in place.
  text,     ///< Append the static \c text verbatim (a bracket, comma, colon).
  key,      ///< Escape \c text (a live object key) as a JSON string.
  indent,   ///< Append \c depth levels of indentation (pretty output only).
  newline,  ///< Append a newline (pretty output only).
};

/**
 * @brief One pending unit of output on the serialisation work stack.
 */
struct emit_step {
  emit_op op{emit_op::render};  ///< What this step emits.
  value const* node{nullptr};   ///< Node to render (for \c emit_op::render).
  std::string_view text{};      ///< Literal or key text (text / key ops).
  std::size_t depth{0};         ///< Nesting depth for indentation.
};

/// @cond INTERNAL

/**
 * @brief Serialise \p root into \p out, compact or pretty.
 *
 * Walks the DOM with an explicit \c emit_step work stack rather than
 * recursion, so an arbitrarily deep hand-built DOM serialises without
 * overflowing the call stack. Object keys are emitted in sorted \c flat_map
 * order for deterministic output.
 *
 * @param out Output string the JSON is appended to.
 * @param root DOM value to serialise.
 * @param opts Formatting options (indent width and \c ascii_only).
 * @param pretty When \c true, emit newlines and indentation.
 *
 * @pre None.
 * @post \p out has grown by the serialised form of \p root.
 */
auto write_value(
  std::string& out, value const& root, serialize_options const& opts, bool const pretty
) -> void;

/// @endcond

}  // namespace detail

/**
 * @brief Serialise \p v to a compact, single-line JSON string.
 *
 * Emits no whitespace. Object keys appear in sorted order, so output is
 * deterministic and diff-friendly. Numbers round-trip via
 * \c std::to_chars; NaN and infinity are written as \c null since JSON
 * cannot represent them. Strings are escaped per JSON rules, with
 * non-ASCII bytes escaped only when \c serialize_options::ascii_only is
 * set.
 *
 * @param v     DOM value to serialise.
 * @param opts  Formatting options; only \c ascii_only affects compact
 *              output.
 *
 * @return The serialised JSON text.
 *
 * @pre None.
 * @post The result describes \p v. String bytes at or above 0x20 are copied
 *       through unvalidated, so with the default options a string that is not
 *       valid UTF-8 yields output that is not valid UTF-8; with
 *       \c serialize_options::ascii_only every non-ASCII byte is decoded and
 *       any malformed sequence becomes U+FFFD, so that output is always valid
 *       JSON.
 *
 * @throws None directly. May propagate \c std::bad_alloc from growing the
 *         output string.
 */
[[nodiscard]] auto serialize(value const& v, serialize_options const& opts = {}) -> std::string;

/**
 * @brief Serialise \p v to indented, human-readable JSON.
 *
 * Identical content to \c serialize but with newlines and
 * \c serialize_options::indent spaces of indentation per nesting level.
 * Keys remain sorted for deterministic output.
 *
 * @param v     DOM value to serialise.
 * @param opts  Formatting options; both \c indent and \c ascii_only apply.
 *
 * @return The pretty-printed JSON text.
 *
 * @pre None.
 * @post The result describes \p v. String bytes at or above 0x20 are copied
 *       through unvalidated, so with the default options a string that is not
 *       valid UTF-8 yields output that is not valid UTF-8; with
 *       \c serialize_options::ascii_only every non-ASCII byte is decoded and
 *       any malformed sequence becomes U+FFFD, so that output is always valid
 *       JSON.
 *
 * @throws None directly. May propagate \c std::bad_alloc from growing the
 *         output string.
 */
[[nodiscard]] auto serialize_pretty(value const& v, serialize_options const& opts = {})
  -> std::string;

}  // namespace nexenne::serialization::json
