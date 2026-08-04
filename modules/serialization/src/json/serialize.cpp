#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nexenne/serialization/json/serialize.hpp>

namespace nexenne::serialization::json {

namespace detail {

auto append_u_escape(std::string& out, std::uint16_t const code) -> void {
  constexpr char hex[]{"0123456789abcdef"};
  out += "\\u";
  out.push_back(hex[(code >> 12) & 0xF]);
  out.push_back(hex[(code >> 8) & 0xF]);
  out.push_back(hex[(code >> 4) & 0xF]);
  out.push_back(hex[code & 0xF]);
}

auto write_escaped_string(std::string& out, std::string_view const s, bool const ascii_only)
  -> void {
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

auto write_number(std::string& out, double const d) -> void {
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

auto write_number(std::string& out, std::int64_t const i) -> void {
  auto buf{std::array<char, 24>{}};
  auto const r{std::to_chars(buf.data(), buf.data() + buf.size(), i)};
  out.append(buf.data(), static_cast<std::size_t>(r.ptr - buf.data()));
}

auto write_value(
  std::string& out, value const& root, serialize_options const& opts, bool const pretty
) -> void {
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
          stack.push_back({.op = emit_op::render, .node = &arr[j], .text = {}, .depth = depth + 1});
          stack.push_back({.op = emit_op::indent, .node = nullptr, .text = {}, .depth = depth + 1});
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
          stack.push_back({.op = emit_op::key, .node = nullptr, .text = entry.first, .depth = 0});
          stack.push_back({.op = emit_op::indent, .node = nullptr, .text = {}, .depth = depth + 1});
        }
        stack.push_back({.op = emit_op::newline, .node = nullptr, .text = {}, .depth = 0});
        stack.push_back({.op = emit_op::text, .node = nullptr, .text = "{", .depth = 0});
        break;
      }
    }
  }
}

}  // namespace detail

auto serialize(value const& v, serialize_options const& opts) -> std::string {
  auto out{std::string{}};
  out.reserve(64);
  detail::write_value(out, v, opts, /*pretty=*/false);
  return out;
}

auto serialize_pretty(value const& v, serialize_options const& opts) -> std::string {
  auto out{std::string{}};
  out.reserve(128);
  detail::write_value(out, v, opts, /*pretty=*/true);
  return out;
}

}  // namespace nexenne::serialization::json
