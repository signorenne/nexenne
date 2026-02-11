#pragma once

/**
 * @file
 * @brief Recursive-descent JSON parser.
 *
 * Strict by default, rejects trailing commas, comments, and
 * NaN/Infinity literals (per RFC 8259). Opt into the common
 * extensions via \c parse_options:
 *
 *   - \c allow_comments        : line and block (C-style) comments
 *   - \c allow_trailing_commas : after the last array/object element
 *
 * Strings are validated as UTF-8 and \\u-escapes are decoded
 * (including surrogate pairs). Numbers without fractional part
 * or exponent are stored as \c int64_t; everything else as
 * \c double.
 *
 * Errors carry a position (offset, line, column) so the caller
 * can produce a useful diagnostic.
 */

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/serialization/error.hpp>
#include <nexenne/serialization/json/value.hpp>

namespace nexenne::serialization::json {

/**
 * @brief Tunable relaxations and limits for \c parse.
 *
 * Defaults are strict RFC 8259. Enable the flags to accept common
 * extensions, and lower \c max_depth to cap recursion on constrained
 * stacks.
 */
struct parse_options {
  bool allow_comments{false};         ///< Accept line and C-style block comments.
  bool allow_trailing_commas{false};  ///< Accept a comma after the last element.
  /// Maximum container nesting depth. The parser recurses per level, so a deep
  /// limit can overflow a small stack; the default is conservative.
  std::size_t max_depth{128};
};

/**
 * @brief Failure detail returned when \c parse cannot produce a value.
 *
 * Carries the error code together with the byte offset and the 1-based
 * line and column where parsing stopped, so callers can render a precise
 * diagnostic.
 */
struct parse_error {
  error code{error::invalid_input};  ///< What went wrong.
  std::size_t offset{0};             ///< Zero-based byte offset of the failure.
  std::size_t line{1};               ///< One-based line number of the failure.
  std::size_t column{1};             ///< One-based column number of the failure.
};

namespace detail {

class parser {
public:
  parser(std::string_view const src, parse_options const opts) noexcept
      : m_src{src}, m_opts{opts} {}

  [[nodiscard]] auto parse() -> std::expected<value, parse_error> {
    skip_ws();
    auto v{parse_value(0)};
    if (!v.has_value()) {
      return std::unexpected{v.error()};
    }
    skip_ws();
    if (m_pos != m_src.size()) {
      return std::unexpected{make_error(error::unexpected_character)};
    }
    if (m_bad_comment) {
      return std::unexpected{make_error(error::unexpected_end)};
    }
    return std::move(*v);
  }

private:
  std::string_view m_src{};
  parse_options m_opts{};
  std::size_t m_pos{0};
  std::size_t m_line{1};
  std::size_t m_col{1};
  bool m_bad_comment{false};  ///< Set when a block comment runs to EOF unterminated.

  [[nodiscard]] auto make_error(error const e) const noexcept -> parse_error {
    return {.code = e, .offset = m_pos, .line = m_line, .column = m_col};
  }

  auto advance() noexcept -> char {
    auto const c{m_src[m_pos++]};
    if (c == '\n') {
      ++m_line;
      m_col = 1;
    } else {
      ++m_col;
    }
    return c;
  }

  auto skip_ws() noexcept -> void {
    while (m_pos < m_src.size()) {
      auto const c{m_src[m_pos]};
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        advance();
        continue;
      }
      if (m_opts.allow_comments && c == '/' && m_pos + 1 < m_src.size()) {
        auto const c2{m_src[m_pos + 1]};
        if (c2 == '/') {
          while (m_pos < m_src.size() && m_src[m_pos] != '\n') {
            advance();
          }
          continue;
        }
        if (c2 == '*') {
          advance();
          advance();
          while (m_pos + 1 < m_src.size() && !(m_src[m_pos] == '*' && m_src[m_pos + 1] == '/')) {
            advance();
          }
          if (m_pos + 1 < m_src.size()) {
            advance();
            advance();
          } else {
            // Unterminated block comment: consume the rest and flag it so the
            // top-level parse rejects the input instead of silently accepting.
            m_pos = m_src.size();
            m_bad_comment = true;
          }
          continue;
        }
      }
      break;
    }
  }

  [[nodiscard]] auto parse_value(std::size_t const depth) -> std::expected<value, parse_error> {
    if (depth >= m_opts.max_depth) {
      return std::unexpected{make_error(error::depth_limit_exceeded)};
    }
    skip_ws();
    if (m_pos >= m_src.size()) {
      return std::unexpected{make_error(error::unexpected_end)};
    }
    auto const c{m_src[m_pos]};
    switch (c) {
      case '{':
        return parse_object(depth + 1);
      case '[':
        return parse_array(depth + 1);
      case '"': {
        auto s{parse_string()};
        if (!s)
          return std::unexpected{s.error()};
        return value{std::move(*s)};
      }
      case 't':
      case 'f':
        return parse_bool();
      case 'n':
        return parse_null();
      case '-':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        return parse_number();
      default:
        return std::unexpected{make_error(error::unexpected_character)};
    }
  }

  [[nodiscard]] auto parse_null() -> std::expected<value, parse_error> {
    if (m_src.substr(m_pos, 4) != "null") {
      return std::unexpected{make_error(error::unexpected_character)};
    }
    for (auto i{0}; i < 4; ++i)
      advance();
    return value{};
  }

  [[nodiscard]] auto parse_bool() -> std::expected<value, parse_error> {
    if (m_src.substr(m_pos, 4) == "true") {
      for (auto i{0}; i < 4; ++i)
        advance();
      return value{true};
    }
    if (m_src.substr(m_pos, 5) == "false") {
      for (auto i{0}; i < 5; ++i)
        advance();
      return value{false};
    }
    return std::unexpected{make_error(error::unexpected_character)};
  }

  [[nodiscard]] auto parse_number() -> std::expected<value, parse_error> {
    auto const start{m_pos};
    auto const is_digit{[this] {
      return m_pos < m_src.size() && m_src[m_pos] >= '0' && m_src[m_pos] <= '9';
    }};
    if (m_src[m_pos] == '-')
      advance();
    // Integer part (RFC 8259): a single 0, or [1-9][0-9]*. No leading zeros.
    if (!is_digit()) {
      return std::unexpected{make_error(error::invalid_number)};
    }
    if (m_src[m_pos] == '0') {
      advance();
      if (is_digit()) {  // a leading zero like "01"
        return std::unexpected{make_error(error::invalid_number)};
      }
    } else {
      while (is_digit())
        advance();
    }
    auto is_float{false};
    if (m_pos < m_src.size() && m_src[m_pos] == '.') {
      is_float = true;
      advance();
      if (!is_digit()) {  // a fraction needs at least one digit, e.g. "1." is invalid
        return std::unexpected{make_error(error::invalid_number)};
      }
      while (is_digit())
        advance();
    }
    if (m_pos < m_src.size() && (m_src[m_pos] == 'e' || m_src[m_pos] == 'E')) {
      is_float = true;
      advance();
      if (m_pos < m_src.size() && (m_src[m_pos] == '+' || m_src[m_pos] == '-')) {
        advance();
      }
      if (!is_digit()) {  // an exponent needs at least one digit, e.g. "1e" is invalid
        return std::unexpected{make_error(error::invalid_number)};
      }
      while (is_digit())
        advance();
    }
    auto const text{m_src.substr(start, m_pos - start)};
    if (is_float) {
      auto out{0.0};
      auto const r{std::from_chars(text.data(), text.data() + text.size(), out)};
      if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) {
        return std::unexpected{make_error(error::invalid_number)};
      }
      return value{out};
    }
    auto out{std::int64_t{0}};
    auto const r{std::from_chars(text.data(), text.data() + text.size(), out)};
    if (r.ec == std::errc::result_out_of_range) {
      // An integer literal beyond int64 range is still a valid JSON number;
      // widen it to double rather than rejecting it.
      auto wide{0.0};
      auto const fr{std::from_chars(text.data(), text.data() + text.size(), wide)};
      if (fr.ec != std::errc{} || fr.ptr != text.data() + text.size()) {
        return std::unexpected{make_error(error::invalid_number)};
      }
      return value{wide};
    }
    if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) {
      return std::unexpected{make_error(error::invalid_number)};
    }
    return value{out};
  }

  [[nodiscard]] auto parse_string() -> std::expected<std::string, parse_error> {
    if (m_src[m_pos] != '"') {
      return std::unexpected{make_error(error::unexpected_character)};
    }
    advance();
    auto out{std::string{}};
    out.reserve(16);
    while (m_pos < m_src.size()) {
      auto const c{m_src[m_pos]};
      if (c == '"') {
        advance();
        return out;
      }
      if (c == '\\') {
        advance();
        if (m_pos >= m_src.size()) {
          return std::unexpected{make_error(error::invalid_escape)};
        }
        auto const esc{advance()};
        switch (esc) {
          case '"':
            out.push_back('"');
            break;
          case '\\':
            out.push_back('\\');
            break;
          case '/':
            out.push_back('/');
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'u': {
            if (m_pos + 4 > m_src.size()) {
              return std::unexpected{make_error(error::invalid_escape)};
            }
            auto cp{std::uint32_t{0}};
            for (auto i{0}; i < 4; ++i) {
              auto const h{advance()};
              cp <<= 4;
              if (h >= '0' && h <= '9')
                cp |= static_cast<std::uint32_t>(h - '0');
              else if (h >= 'a' && h <= 'f')
                cp |= static_cast<std::uint32_t>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F')
                cp |= static_cast<std::uint32_t>(h - 'A' + 10);
              else
                return std::unexpected{make_error(error::invalid_escape)};
            }
            // Surrogate-pair handling for non-BMP code points.
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              if (m_pos + 6 > m_src.size() || m_src[m_pos] != '\\' || m_src[m_pos + 1] != 'u') {
                return std::unexpected{make_error(error::invalid_escape)};
              }
              advance();
              advance();
              auto low{std::uint32_t{0}};
              for (auto i{0}; i < 4; ++i) {
                auto const h{advance()};
                low <<= 4;
                if (h >= '0' && h <= '9')
                  low |= static_cast<std::uint32_t>(h - '0');
                else if (h >= 'a' && h <= 'f')
                  low |= static_cast<std::uint32_t>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                  low |= static_cast<std::uint32_t>(h - 'A' + 10);
                else
                  return std::unexpected{make_error(error::invalid_escape)};
              }
              // The second escape must be a low surrogate; otherwise the
              // pair is invalid (and (low, 0xDC00) would underflow below).
              if (low < 0xDC00 || low > 0xDFFF) {
                return std::unexpected{make_error(error::invalid_escape)};
              }
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              // A lone low surrogate is not a valid scalar value.
              return std::unexpected{make_error(error::invalid_escape)};
            }
            // Encode as UTF-8.
            if (cp < 0x80) {
              out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
              out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default:
            return std::unexpected{make_error(error::invalid_escape)};
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        return std::unexpected{make_error(error::invalid_string)};
      } else {
        out.push_back(c);
        advance();
      }
    }
    return std::unexpected{make_error(error::unexpected_end)};
  }

  [[nodiscard]] auto parse_array(std::size_t const depth) -> std::expected<value, parse_error> {
    advance();  // '['
    array arr{};
    skip_ws();
    if (m_pos < m_src.size() && m_src[m_pos] == ']') {
      advance();
      return value{std::move(arr)};
    }
    while (true) {
      auto elem{parse_value(depth)};
      if (!elem)
        return std::unexpected{elem.error()};
      arr.push_back(std::move(*elem));
      skip_ws();
      if (m_pos >= m_src.size()) {
        return std::unexpected{make_error(error::unexpected_end)};
      }
      if (m_src[m_pos] == ',') {
        advance();
        skip_ws();
        if (m_opts.allow_trailing_commas && m_pos < m_src.size() && m_src[m_pos] == ']') {
          advance();
          return value{std::move(arr)};
        }
        continue;
      }
      if (m_src[m_pos] == ']') {
        advance();
        return value{std::move(arr)};
      }
      return std::unexpected{make_error(error::unexpected_character)};
    }
  }

  [[nodiscard]] auto parse_object(std::size_t const depth) -> std::expected<value, parse_error> {
    advance();  // '{'
    object obj{};
    skip_ws();
    if (m_pos < m_src.size() && m_src[m_pos] == '}') {
      advance();
      return value{std::move(obj)};
    }
    while (true) {
      skip_ws();
      if (m_pos >= m_src.size() || m_src[m_pos] != '"') {
        return std::unexpected{make_error(error::unexpected_character)};
      }
      auto key{parse_string()};
      if (!key)
        return std::unexpected{key.error()};
      skip_ws();
      if (m_pos >= m_src.size() || m_src[m_pos] != ':') {
        return std::unexpected{make_error(error::unexpected_character)};
      }
      advance();
      auto val{parse_value(depth)};
      if (!val)
        return std::unexpected{val.error()};
      auto const [it, inserted]{obj.try_emplace(std::move(*key), std::move(*val))};
      if (!inserted) {
        return std::unexpected{make_error(error::duplicate_key)};
      }
      skip_ws();
      if (m_pos >= m_src.size()) {
        return std::unexpected{make_error(error::unexpected_end)};
      }
      if (m_src[m_pos] == ',') {
        advance();
        skip_ws();
        if (m_opts.allow_trailing_commas && m_pos < m_src.size() && m_src[m_pos] == '}') {
          advance();
          return value{std::move(obj)};
        }
        continue;
      }
      if (m_src[m_pos] == '}') {
        advance();
        return value{std::move(obj)};
      }
      return std::unexpected{make_error(error::unexpected_character)};
    }
  }
};

}  // namespace detail

/**
 * @brief Parse \p input as JSON into a DOM \c value.
 *
 * Recursive-descent parse honouring \p opts. Strings are validated as
 * UTF-8 with \\u-escapes (including surrogate pairs) decoded; numbers
 * without a fractional part or exponent become \c int64_t, the rest
 * \c double. The whole input must be consumed, trailing non-whitespace is
 * an error.
 *
 * @param input  The JSON text to parse.
 * @param opts   Parsing relaxations and the depth limit; strict by
 *               default.
 *
 * @return The parsed DOM tree on success, or a \c parse_error locating the
 *         first failure.
 *
 * @pre None.
 * @post On success the returned tree owns all of its data and \p input is
 *       no longer referenced; on failure the error offset is within
 *       \c input.size().
 *
 * @throws None directly. May propagate \c std::bad_alloc from building the
 *         DOM, since the parser allocates strings, arrays, and objects.
 */
[[nodiscard]] inline auto parse(std::string_view const input, parse_options const opts = {})
  -> std::expected<value, parse_error> {
  auto p{detail::parser{input, opts}};
  return p.parse();
}

}  // namespace nexenne::serialization::json
