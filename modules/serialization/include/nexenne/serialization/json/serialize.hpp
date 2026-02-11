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
 * Keys are emitted in the \c std::map iteration order (sorted), so
 * the output is deterministic and stable for diffing. Numbers use
 * \c std::to_chars for round-trippable representations. Strings
 * escape the JSON-required set (the quote, the backslash, and the
 * control characters) but pass valid UTF-8 through untouched.
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

// Append "\uXXXX" for a 16-bit code unit, manual hex so no snprintf or locale
// is pulled in (faster, and freestanding-friendly).
inline auto append_u_escape(std::string& out, std::uint16_t const code) -> void {
  constexpr char hex[]{"0123456789abcdef"};
  out += "\\u";
  out.push_back(hex[(code >> 12) & 0xF]);
  out.push_back(hex[(code >> 8) & 0xF]);
  out.push_back(hex[(code >> 4) & 0xF]);
  out.push_back(hex[code & 0xF]);
}

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
      // in 0x80..0xBF, or too few bytes) becomes U+FFFD, so the output stays
      // valid ASCII instead of echoing raw bytes or misreading the next one.
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

inline auto write_number(std::string& out, std::int64_t const i) -> void {
  auto buf{std::array<char, 24>{}};
  auto const r{std::to_chars(buf.data(), buf.data() + buf.size(), i)};
  out.append(buf.data(), static_cast<std::size_t>(r.ptr - buf.data()));
}

inline auto write_value(
  std::string& out,
  value const& v,
  serialize_options const& opts,
  bool const pretty,
  std::size_t const depth
) -> void {
  auto const newline{[&] {
    if (pretty)
      out.push_back('\n');
  }};
  auto const indent{[&](std::size_t const d) {
    if (pretty)
      out.append(d * opts.indent, ' ');
  }};

  switch (v.type()) {
    case value::kind::null_kind:
      out += "null";
      return;
    case value::kind::boolean_kind:
      out += (*v.as_bool()) ? "true" : "false";
      return;
    case value::kind::integer_kind:
      write_number(out, *v.as_int());
      return;
    case value::kind::floating_kind:
      write_number(out, *v.as_float());
      return;
    case value::kind::string_kind:
      write_escaped_string(out, *v.as_string(), opts.ascii_only);
      return;
    case value::kind::array_kind: {
      auto const& arr{v.as_array()->get()};
      if (arr.empty()) {
        out += "[]";
        return;
      }
      out.push_back('[');
      newline();
      for (std::size_t i{0}; i < arr.size(); ++i) {
        indent(depth + 1);
        write_value(out, arr[i], opts, pretty, depth + 1);
        if (i + 1 < arr.size())
          out.push_back(',');
        newline();
      }
      indent(depth);
      out.push_back(']');
      return;
    }
    case value::kind::object_kind: {
      auto const& obj{v.as_object()->get()};
      if (obj.empty()) {
        out += "{}";
        return;
      }
      out.push_back('{');
      newline();
      auto const last{obj.size() - 1};
      auto i{std::size_t{0}};
      for (auto const& [k, val] : obj) {
        indent(depth + 1);
        write_escaped_string(out, k, opts.ascii_only);
        out.push_back(':');
        if (pretty)
          out.push_back(' ');
        write_value(out, val, opts, pretty, depth + 1);
        if (i++ < last)
          out.push_back(',');
        newline();
      }
      indent(depth);
      out.push_back('}');
      return;
    }
  }
}

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
 * @post The result is valid JSON describing \p v.
 *
 * @throws None directly. May propagate \c std::bad_alloc from growing the
 *         output string.
 */
[[nodiscard]] inline auto
serialize(value const& v, serialize_options const& opts = {}) -> std::string {
  auto out{std::string{}};
  out.reserve(64);
  detail::write_value(out, v, opts, /*pretty=*/false, /*depth=*/0);
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
 * @post The result is valid JSON describing \p v.
 *
 * @throws None directly. May propagate \c std::bad_alloc from growing the
 *         output string.
 */
[[nodiscard]] inline auto
serialize_pretty(value const& v, serialize_options const& opts = {}) -> std::string {
  auto out{std::string{}};
  out.reserve(128);
  detail::write_value(out, v, opts, /*pretty=*/true, /*depth=*/0);
  return out;
}

}  // namespace nexenne::serialization::json
