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
 * Strings are decoded: \\u-escapes (including surrogate pairs)
 * become UTF-8, but the parser does NOT validate raw string
 * bytes. Any byte at or above 0x20 is copied through unchecked,
 * so it is the caller's responsibility to ensure a string is
 * well-formed UTF-8. Numbers without a fractional part or
 * exponent are stored as \c int64_t; everything else as
 * \c double. A number whose magnitude is too small for a
 * \c double underflows to a signed zero (still a valid JSON
 * value); one whose magnitude is too large to represent is
 * rejected with \c error::invalid_number rather than stored as
 * infinity.
 *
 * Errors carry a position (offset, line, column) so the caller
 * can produce a useful diagnostic.
 */

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/serialization/error.hpp>
#include <nexenne/serialization/json/value.hpp>
#include <nexenne/utility/buffer_cursor.hpp>

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
  /**
   * @brief Maximum container nesting depth.
   *
   * Counts open arrays and objects only: a top-level scalar has depth zero, so
   * \c max_depth of \c N admits exactly \c N nested containers. The parser
   * recurses one call frame per open container, so a deep limit can overflow a
   * small stack; the default is conservative.
   */
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

/// @cond INTERNAL
namespace detail {

/**
 * @brief Recursive-descent JSON parser driving the public \c parse.
 */
class parser {
public:
  /**
   * @brief Construct a parser over \p src with options \p opts.
   *
   * @param src JSON text to parse. Must outlive the parser.
   * @param opts Parsing relaxations and the depth limit.
   *
   * @pre \p src refers to valid characters for the lifetime of the parser.
   * @post The cursor is at offset zero and the line/column are 1.
   */
  parser(std::string_view const src, parse_options const opts) noexcept
      : m_cursor{std::span<char const>{src.data(), src.size()}}, m_opts{opts} {}

  /**
   * @brief Parse the whole input into a DOM value.
   *
   * Parses one top-level value and requires the rest of the input to be
   * whitespace, so trailing non-whitespace (or an unterminated block comment)
   * is an error.
   *
   * @return The parsed value on success, or a \c parse_error locating the
   *         failure.
   *
   * @pre None.
   * @post On success the whole input has been consumed.
   *
   * @throws None directly. May propagate \c std::bad_alloc from building the
   *         DOM.
   */
  [[nodiscard]] auto parse() -> std::expected<value, parse_error> {
    skip_ws();
    auto v{parse_value(0)};
    if (!v.has_value()) {
      return std::unexpected{v.error()};
    }
    skip_ws();
    if (!m_cursor.exhausted()) {
      return std::unexpected{make_error(error::unexpected_character)};
    }
    if (m_bad_comment) {
      return std::unexpected{make_error(error::unexpected_end)};
    }
    return std::move(*v);
  }

private:
  utility::buffer_cursor<char const> m_cursor;
  parse_options m_opts{};
  std::size_t m_line{1};
  std::size_t m_col{1};
  bool m_bad_comment{false};  ///< Set when a block comment runs to EOF unterminated.

  /**
   * @brief Build a \c parse_error at the current cursor location.
   *
   * @param e Error code to attach.
   *
   * @return A \c parse_error carrying \p e and the current offset, line, and
   *         column.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto make_error(error const e) const noexcept -> parse_error {
    return {.code = e, .offset = m_cursor.position(), .line = m_line, .column = m_col};
  }

  /**
   * @brief Whether the source at the cursor begins with keyword \p lit.
   *
   * Mirrors the old \c substr(pos, len) == lit test: a clamped substring can
   * never equal \p lit unless \c lit.size() bytes remain.
   *
   * @param lit Keyword literal to match (for example \c "null").
   *
   * @return \c true when the next bytes equal \p lit.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto matches_literal(std::string_view const lit) const noexcept -> bool {
    return m_cursor.has(lit.size()) && std::string_view{m_cursor.data(), lit.size()} == lit;
  }

  /**
   * @brief Consume one character and update the line / column counters.
   *
   * @return The character consumed.
   *
   * @pre At least one byte remains at the cursor.
   * @post The cursor advances by one; the line and column reflect the byte.
   */
  auto advance() noexcept -> char {
    auto const c{m_cursor.next()};
    if (c == '\n') {
      ++m_line;
      m_col = 1;
    } else {
      ++m_col;
    }
    return c;
  }

  /**
   * @brief Skip whitespace, and comments when \c allow_comments is set.
   *
   * Advances past spaces, tabs, and newlines; with comments enabled it also
   * consumes line and C-style block comments. An unterminated block comment
   * is flagged so the top-level parse rejects it.
   *
   * @pre None.
   * @post The cursor sits on the next significant byte or at end of input.
   */
  auto skip_ws() noexcept -> void {
    while (!m_cursor.exhausted()) {
      auto const c{m_cursor.data()[0]};
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        advance();
        continue;
      }
      if (m_opts.allow_comments && c == '/' && m_cursor.has(2)) {
        auto const c2{m_cursor.data()[1]};
        if (c2 == '/') {
          while (!m_cursor.exhausted() && m_cursor.data()[0] != '\n') {
            advance();
          }
          continue;
        }
        if (c2 == '*') {
          advance();
          advance();
          while (m_cursor.has(2) && !(m_cursor.data()[0] == '*' && m_cursor.data()[1] == '/')) {
            advance();
          }
          if (m_cursor.has(2)) {
            advance();
            advance();
          } else {
            // Unterminated block comment: consume the rest and flag it so the
            // top-level parse rejects the input instead of silently accepting.
            m_cursor.seek(m_cursor.size());
            m_bad_comment = true;
          }
          continue;
        }
      }
      break;
    }
  }

  /**
   * @brief Parse one JSON value, dispatching on the leading character.
   *
   * \p depth is the number of arrays and objects already open around this
   * value; \c parse_array and \c parse_object enforce \c max_depth on entry so
   * the limit counts open containers exactly (a top-level scalar sits at depth
   * zero).
   *
   * @param depth Number of containers already open around this value.
   *
   * @return The parsed value on success, or a \c parse_error on failure.
   *
   * @pre None.
   * @post On success the cursor sits just past the value.
   */
  [[nodiscard]] auto parse_value(std::size_t const depth) -> std::expected<value, parse_error> {
    skip_ws();
    if (m_cursor.exhausted()) {
      return std::unexpected{make_error(error::unexpected_end)};
    }
    auto const c{m_cursor.data()[0]};
    switch (c) {
      case '{':
        return parse_object(depth);
      case '[':
        return parse_array(depth);
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

  /**
   * @brief Parse the \c null literal.
   *
   * @return A null value on success, or a \c parse_error on failure.
   *
   * @pre The cursor is at the start of a \c null literal candidate.
   * @post On success the cursor sits just past \c null.
   */
  [[nodiscard]] auto parse_null() -> std::expected<value, parse_error> {
    if (!matches_literal("null")) {
      return std::unexpected{make_error(error::unexpected_character)};
    }
    for (auto i{0}; i < 4; ++i)
      advance();
    return value{};
  }

  /**
   * @brief Parse the \c true or \c false literal.
   *
   * @return A boolean value on success, or a \c parse_error on failure.
   *
   * @pre The cursor is at the start of a boolean literal candidate.
   * @post On success the cursor sits just past the literal.
   */
  [[nodiscard]] auto parse_bool() -> std::expected<value, parse_error> {
    if (matches_literal("true")) {
      for (auto i{0}; i < 4; ++i)
        advance();
      return value{true};
    }
    if (matches_literal("false")) {
      for (auto i{0}; i < 5; ++i)
        advance();
      return value{false};
    }
    return std::unexpected{make_error(error::unexpected_character)};
  }

  /**
   * @brief Whether an out-of-range number literal overflows (vs underflows).
   *
   * Decides whether a grammatically valid literal that \c std::from_chars
   * reported as out of double's range is too large (overflow) or too small
   * (underflow). Such a value is always extreme, never near 1, so the sign of
   * its decimal order of magnitude separates the two: an order at or above
   * zero is an overflow, below zero an underflow.
   *
   * @param text The full number literal.
   *
   * @return \c true when the magnitude is too large to represent (overflow),
   *         \c false when too small (underflow).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static auto number_overflows(std::string_view text) noexcept -> bool {
    if (!text.empty() && text.front() == '-') {
      text.remove_prefix(1);
    }
    auto exp10{std::int64_t{0}};
    auto sig{text};
    if (auto const e{text.find_first_of("eE")}; e != std::string_view::npos) {
      sig = text.substr(0, e);
      auto et{text.substr(e + 1)};
      auto const neg{!et.empty() && et.front() == '-'};
      if (!et.empty() && (et.front() == '+' || et.front() == '-')) {
        et.remove_prefix(1);
      }
      for (auto const ch : et) {
        if (exp10 < 1'000'000) {  // saturate: only the order's sign matters here
          exp10 = exp10 * 10 + (ch - '0');
        }
      }
      if (neg) {
        exp10 = -exp10;
      }
    }
    auto ipart{sig};
    auto fpart{std::string_view{}};
    if (auto const dot{sig.find('.')}; dot != std::string_view::npos) {
      ipart = sig.substr(0, dot);
      fpart = sig.substr(dot + 1);
    }
    // A nonzero integer part (JSON forbids leading zeros) puts the leading digit
    // at power ipart.size() - 1; otherwise it is the first nonzero fraction digit.
    if (ipart != "0") {
      return static_cast<std::int64_t>(ipart.size()) - 1 + exp10 >= 0;
    }
    auto k{std::size_t{0}};
    while (k < fpart.size() && fpart[k] == '0') {
      ++k;
    }
    return exp10 - static_cast<std::int64_t>(k + 1) >= 0;
  }

  /**
   * @brief Parse a JSON number literal into an integer or floating value.
   *
   * Validates the RFC 8259 grammar, then converts with \c std::from_chars: a
   * literal with no fraction or exponent becomes an \c int64_t (widened to
   * \c double when it exceeds the integer range), otherwise a \c double. A
   * magnitude too small for a \c double underflows to a signed zero, while one
   * too large is rejected with \c error::invalid_number.
   *
   * @return The parsed numeric value on success, or a \c parse_error on
   *         failure.
   *
   * @pre The cursor is at a digit or a leading minus sign.
   * @post On success the cursor sits just past the number literal.
   */
  [[nodiscard]] auto parse_number() -> std::expected<value, parse_error> {
    auto const start{m_cursor.position()};
    auto const is_digit{[this] {
      return !m_cursor.exhausted() && m_cursor.data()[0] >= '0' && m_cursor.data()[0] <= '9';
    }};
    if (m_cursor.data()[0] == '-')
      advance();
    // Integer part (RFC 8259): a single 0, or [1-9][0-9]*. No leading zeros.
    if (!is_digit()) {
      return std::unexpected{make_error(error::invalid_number)};
    }
    if (m_cursor.data()[0] == '0') {
      advance();
      if (is_digit()) {  // a leading zero like "01"
        return std::unexpected{make_error(error::invalid_number)};
      }
    } else {
      while (is_digit())
        advance();
    }
    auto is_float{false};
    if (!m_cursor.exhausted() && m_cursor.data()[0] == '.') {
      is_float = true;
      advance();
      if (!is_digit()) {  // a fraction needs at least one digit, e.g. "1." is invalid
        return std::unexpected{make_error(error::invalid_number)};
      }
      while (is_digit())
        advance();
    }
    if (!m_cursor.exhausted() && (m_cursor.data()[0] == 'e' || m_cursor.data()[0] == 'E')) {
      is_float = true;
      advance();
      if (!m_cursor.exhausted() && (m_cursor.data()[0] == '+' || m_cursor.data()[0] == '-')) {
        advance();
      }
      if (!is_digit()) {  // an exponent needs at least one digit, e.g. "1e" is invalid
        return std::unexpected{make_error(error::invalid_number)};
      }
      while (is_digit())
        advance();
    }
    auto const text{std::string_view{m_cursor.buffer().data() + start, m_cursor.position() - start}
    };
    if (is_float) {
      auto out{0.0};
      auto const r{std::from_chars(text.data(), text.data() + text.size(), out)};
      if (r.ec == std::errc::result_out_of_range) {
        // A grammatically valid number outside double's range: a magnitude too
        // small to represent underflows to a signed zero (still a valid JSON
        // value), while one too large to represent is rejected rather than
        // stored as infinity.
        if (number_overflows(text)) {
          return std::unexpected{make_error(error::invalid_number)};
        }
        return value{text.front() == '-' ? -0.0 : 0.0};
      }
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

  /**
   * @brief Parse a JSON string literal, decoding escapes to UTF-8.
   *
   * Decodes the standard escapes and \c \\u escapes, including surrogate
   * pairs, into UTF-8. Bytes at or above 0x20 are copied through unchecked, so
   * raw UTF-8 validity is the caller's responsibility.
   *
   * @return The decoded string on success, or a \c parse_error on failure.
   *
   * @pre The cursor is at the opening double quote.
   * @post On success the cursor sits just past the closing quote.
   *
   * @throws None directly. May propagate \c std::bad_alloc from growing the
   *         decoded string.
   */
  [[nodiscard]] auto parse_string() -> std::expected<std::string, parse_error> {
    if (m_cursor.data()[0] != '"') {
      return std::unexpected{make_error(error::unexpected_character)};
    }
    advance();
    auto out{std::string{}};
    out.reserve(16);
    while (!m_cursor.exhausted()) {
      auto const c{m_cursor.data()[0]};
      if (c == '"') {
        advance();
        return out;
      }
      if (c == '\\') {
        advance();
        if (m_cursor.exhausted()) {
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
            if (!m_cursor.has(4)) {
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
              if (!m_cursor.has(6) || m_cursor.data()[0] != '\\' || m_cursor.data()[1] != 'u') {
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

  /**
   * @brief Parse a JSON array, recursing one level per element.
   *
   * Enforces \c max_depth on entry and honours \c allow_trailing_commas.
   *
   * @param depth Number of containers already open around this array.
   *
   * @return The parsed array value on success, or a \c parse_error on failure.
   *
   * @pre The cursor is at the opening bracket.
   * @post On success the cursor sits just past the closing bracket.
   *
   * @throws None directly. May propagate \c std::bad_alloc from building the
   *         array.
   */
  [[nodiscard]] auto parse_array(std::size_t const depth) -> std::expected<value, parse_error> {
    if (depth >= m_opts.max_depth) {
      return std::unexpected{make_error(error::depth_limit_exceeded)};
    }
    advance();  // '['
    array arr{};
    skip_ws();
    if (!m_cursor.exhausted() && m_cursor.data()[0] == ']') {
      advance();
      return value{std::move(arr)};
    }
    while (true) {
      auto elem{parse_value(depth + 1)};
      if (!elem)
        return std::unexpected{elem.error()};
      arr.push_back(std::move(*elem));
      skip_ws();
      if (m_cursor.exhausted()) {
        return std::unexpected{make_error(error::unexpected_end)};
      }
      if (m_cursor.data()[0] == ',') {
        advance();
        skip_ws();
        if (m_opts.allow_trailing_commas && !m_cursor.exhausted() && m_cursor.data()[0] == ']') {
          advance();
          return value{std::move(arr)};
        }
        continue;
      }
      if (m_cursor.data()[0] == ']') {
        advance();
        return value{std::move(arr)};
      }
      return std::unexpected{make_error(error::unexpected_character)};
    }
  }

  /**
   * @brief Parse a JSON object, recursing one level per member value.
   *
   * Enforces \c max_depth on entry, rejects a duplicate key with
   * \c error::duplicate_key, and honours \c allow_trailing_commas.
   *
   * @param depth Number of containers already open around this object.
   *
   * @return The parsed object value on success, or a \c parse_error on
   *         failure.
   *
   * @pre The cursor is at the opening brace.
   * @post On success the cursor sits just past the closing brace.
   *
   * @throws None directly. May propagate \c std::bad_alloc from building the
   *         object.
   */
  [[nodiscard]] auto parse_object(std::size_t const depth) -> std::expected<value, parse_error> {
    if (depth >= m_opts.max_depth) {
      return std::unexpected{make_error(error::depth_limit_exceeded)};
    }
    advance();  // '{'
    object obj{};
    skip_ws();
    if (!m_cursor.exhausted() && m_cursor.data()[0] == '}') {
      advance();
      return value{std::move(obj)};
    }
    while (true) {
      skip_ws();
      if (m_cursor.exhausted() || m_cursor.data()[0] != '"') {
        return std::unexpected{make_error(error::unexpected_character)};
      }
      auto key{parse_string()};
      if (!key)
        return std::unexpected{key.error()};
      skip_ws();
      if (m_cursor.exhausted() || m_cursor.data()[0] != ':') {
        return std::unexpected{make_error(error::unexpected_character)};
      }
      advance();
      auto val{parse_value(depth + 1)};
      if (!val)
        return std::unexpected{val.error()};
      auto const [it, inserted]{obj.try_emplace(std::move(*key), std::move(*val))};
      if (!inserted) {
        return std::unexpected{make_error(error::duplicate_key)};
      }
      skip_ws();
      if (m_cursor.exhausted()) {
        return std::unexpected{make_error(error::unexpected_end)};
      }
      if (m_cursor.data()[0] == ',') {
        advance();
        skip_ws();
        if (m_opts.allow_trailing_commas && !m_cursor.exhausted() && m_cursor.data()[0] == '}') {
          advance();
          return value{std::move(obj)};
        }
        continue;
      }
      if (m_cursor.data()[0] == '}') {
        advance();
        return value{std::move(obj)};
      }
      return std::unexpected{make_error(error::unexpected_character)};
    }
  }
};

}  // namespace detail
/// @endcond

/**
 * @brief Parse \p input as JSON into a DOM \c value.
 *
 * Recursive-descent parse honouring \p opts. String \\u-escapes (including
 * surrogate pairs) are decoded to UTF-8, but raw string bytes are NOT
 * validated as UTF-8: any byte at or above 0x20 is copied through
 * unchecked, so the caller owns UTF-8 correctness. Numbers without a
 * fractional part or exponent become \c int64_t, the rest \c double; a
 * magnitude too small for a \c double underflows to a signed zero, while
 * one too large is rejected with \c error::invalid_number. The whole input
 * must be consumed, trailing non-whitespace is an error.
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
