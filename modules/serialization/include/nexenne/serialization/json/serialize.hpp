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
inline auto append_u_escape(std::string& out, std::uint16_t const code) -> void {
  constexpr char hex[]{"0123456789abcdef"};
  out += "\\u";
  out.push_back(hex[(code >> 12) & 0xF]);
  out.push_back(hex[(code >> 8) & 0xF]);
  out.push_back(hex[(code >> 4) & 0xF]);
  out.push_back(hex[code & 0xF]);
}

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
inline auto
write_escaped_string(std::string& out, std::string_view const s, bool const ascii_only) -> void {
  out.push_back('"');
  for (std::size_t i{0}; i < s.size(); ++i) {
    auto const c{static_cast<unsigned char>(s[i])};
    switch (c) {
      case '"':
        out += "\\\"";
        continue;
      case '\\':
        out += "\\\\";
        continue;
      case '\b':
        out += "\\b";
        continue;
      case '\f':
        out += "\\f";
        continue;
      case '\n':
        out += "\\n";
        continue;
      case '\r':
        out += "\\r";
        continue;
      case '\t':
        out += "\\t";
        continue;
      default:
        break;
    }
    if (c < 0x20) {
      append_u_escape(out, c);
      continue;
    }
    if (ascii_only && c >= 0x80) {
      // Decode one UTF-8 sequence to a code point, then re-emit as \uXXXX. A
      // malformed or truncated sequence (bad lead byte, a continuation byte not
      // in 0x80..0xBF, too few bytes, an overlong encoding, a surrogate, or a
      // code point past U+10FFFF) becomes U+FFFD, so the output stays valid
      // ASCII (and reparses) instead of echoing raw bytes, misreading the next
      // one, or emitting an unpaired or out-of-range surrogate escape.
      auto cp{std::uint32_t{0}};
      auto extra{0};
      if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        extra = 1;
      } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        extra = 2;
      } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07;
        extra = 3;
      } else {
        extra = -1;  // stray continuation byte or invalid lead
      }
      auto valid{extra >= 1};
      for (auto k{0}; valid && k < extra; ++k) {
        if (i + 1 >= s.size()) {
          valid = false;
          break;
        }
        auto const cont{static_cast<unsigned char>(s[i + 1])};
        if ((cont & 0xC0) != 0x80) {
          valid = false;  // not a continuation byte; do not consume it
          break;
        }
        ++i;
        cp = (cp << 6) | (cont & 0x3Fu);
      }
      if (valid) {
        // Reject overlong encodings (a code point below the minimum the
        // sequence length can carry), UTF-16 surrogates, and code points past
        // U+10FFFF: none is a valid Unicode scalar, so none may be escaped.
        auto const min_cp{
          extra == 1 ? std::uint32_t{0x80}
                     : (extra == 2 ? std::uint32_t{0x800} : std::uint32_t{0x10000})
        };
        if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
          valid = false;
        }
      }
      if (!valid) {
        cp = 0xFFFD;  // Unicode replacement character
      }
      if (cp >= 0x10000) {
        auto const adj{cp - 0x10000};
        append_u_escape(out, static_cast<std::uint16_t>(0xD800 + (adj >> 10)));
        append_u_escape(out, static_cast<std::uint16_t>(0xDC00 + (adj & 0x3FF)));
      } else {
        append_u_escape(out, static_cast<std::uint16_t>(cp));
      }
      continue;
    }
    out.push_back(static_cast<char>(c));
  }
  out.push_back('"');
}

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
inline auto write_number(std::string& out, double const d) -> void {
  if (std::isnan(d) || std::isinf(d)) {
    out += "null";  // JSON has no NaN/Infinity; null is the closest legal value.
    return;
  }
  auto buf{std::array<char, 32>{}};
  auto const r{std::to_chars(buf.data(), buf.data() + buf.size(), d)};
  std::string_view const num{buf.data(), static_cast<std::size_t>(r.ptr - buf.data())};
  out.append(num);
  // Keep it a JSON float: to_chars can render a large magnitude in pure integer
  // form, which would reparse as an integer and overflow. Append ".0".
  if (num.find('.') == std::string_view::npos && num.find('e') == std::string_view::npos
      && num.find('E') == std::string_view::npos) {
    out += ".0";
  }
}

/**
 * @brief Append the decimal rendering of a signed integer \p i to \p out.
 *
 * @param out Output string the number is appended to.
 * @param i Value to render.
 *
 * @pre None.
 * @post \p out has grown by the rendered integer.
 */
inline auto write_number(std::string& out, std::int64_t const i) -> void {
  auto buf{std::array<char, 24>{}};
  auto const r{std::to_chars(buf.data(), buf.data() + buf.size(), i)};
  out.append(buf.data(), static_cast<std::size_t>(r.ptr - buf.data()));
}

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
  emit_op op{emit_op::render};      ///< What this step emits.
  value const* node{nullptr};       ///< Node to render (for \c emit_op::render).
  std::string_view text{};          ///< Literal or key text (text / key ops).
  std::size_t depth{0};             ///< Nesting depth for indentation.
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
inline auto
write_value(std::string& out, value const& root, serialize_options const& opts, bool const pretty)
  -> void {
  auto stack{std::vector<emit_step>{}};
  stack.push_back({.op = emit_op::render, .node = &root, .text = {}, .depth = 0});
  while (!stack.empty()) {
    auto const step{stack.back()};
    stack.pop_back();
    switch (step.op) {
      case emit_op::text:
        out += step.text;
        continue;
      case emit_op::key:
        write_escaped_string(out, step.text, opts.ascii_only);
        continue;
      case emit_op::newline:
        if (pretty) {
          out.push_back('\n');
        }
        continue;
      case emit_op::indent:
        if (pretty) {
          out.append(step.depth * opts.indent, ' ');
        }
        continue;
      case emit_op::render:
        break;
    }

    auto const& v{*step.node};
    auto const depth{step.depth};
    switch (v.type()) {
      case value::kind::null_kind:
        out += "null";
        break;
      case value::kind::boolean_kind:
        out += (*v.as_bool()) ? "true" : "false";
        break;
      case value::kind::integer_kind:
        write_number(out, *v.as_int());
        break;
      case value::kind::floating_kind:
        write_number(out, *v.as_float());
        break;
      case value::kind::string_kind:
        write_escaped_string(out, *v.as_string(), opts.ascii_only);
        break;
      case value::kind::array_kind: {
        auto const& arr{v.as_array()->get()};
        if (arr.empty()) {
          out += "[]";
          break;
        }
        // Push the tail first: a LIFO stack replays the pushes in reverse, so
        // the closing bracket lands last and element 0 first.
        stack.push_back({.op = emit_op::text, .node = nullptr, .text = "]", .depth = 0});
        stack.push_back({.op = emit_op::indent, .node = nullptr, .text = {}, .depth = depth});
        for (auto j{arr.size()}; j-- > 0;) {
          stack.push_back({.op = emit_op::newline, .node = nullptr, .text = {}, .depth = 0});
          if (j + 1 < arr.size()) {
            stack.push_back({.op = emit_op::text, .node = nullptr, .text = ",", .depth = 0});
          }
          stack.push_back(
            {.op = emit_op::render, .node = &arr[j], .text = {}, .depth = depth + 1}
          );
          stack.push_back(
            {.op = emit_op::indent, .node = nullptr, .text = {}, .depth = depth + 1}
          );
        }
        stack.push_back({.op = emit_op::newline, .node = nullptr, .text = {}, .depth = 0});
        stack.push_back({.op = emit_op::text, .node = nullptr, .text = "[", .depth = 0});
        break;
      }
      case value::kind::object_kind: {
        auto const& obj{v.as_object()->get()};
        if (obj.empty()) {
          out += "{}";
          break;
        }
        stack.push_back({.op = emit_op::text, .node = nullptr, .text = "}", .depth = 0});
        stack.push_back({.op = emit_op::indent, .node = nullptr, .text = {}, .depth = depth});
        auto const first{obj.begin()};  // flat_map is vector-backed: random access.
        for (auto j{obj.size()}; j-- > 0;) {
          auto const& entry{first[static_cast<std::ptrdiff_t>(j)]};
          stack.push_back({.op = emit_op::newline, .node = nullptr, .text = {}, .depth = 0});
          if (j + 1 < obj.size()) {
            stack.push_back({.op = emit_op::text, .node = nullptr, .text = ",", .depth = 0});
          }
          stack.push_back(
            {.op = emit_op::render, .node = &entry.second, .text = {}, .depth = depth + 1}
          );
          // The colon (and a space when pretty) sits between key and value.
          stack.push_back(
            {.op = emit_op::text, .node = nullptr, .text = pretty ? ": " : ":", .depth = 0}
          );
          stack.push_back(
            {.op = emit_op::key, .node = nullptr, .text = entry.first, .depth = 0}
          );
          stack.push_back(
            {.op = emit_op::indent, .node = nullptr, .text = {}, .depth = depth + 1}
          );
        }
        stack.push_back({.op = emit_op::newline, .node = nullptr, .text = {}, .depth = 0});
        stack.push_back({.op = emit_op::text, .node = nullptr, .text = "{", .depth = 0});
        break;
      }
    }
  }
}

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
[[nodiscard]] inline auto
serialize(value const& v, serialize_options const& opts = {}) -> std::string {
  auto out{std::string{}};
  out.reserve(64);
  detail::write_value(out, v, opts, /*pretty=*/false);
  return out;
}

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
[[nodiscard]] inline auto
serialize_pretty(value const& v, serialize_options const& opts = {}) -> std::string {
  auto out{std::string{}};
  out.reserve(128);
  detail::write_value(out, v, opts, /*pretty=*/true);
  return out;
}

}  // namespace nexenne::serialization::json
