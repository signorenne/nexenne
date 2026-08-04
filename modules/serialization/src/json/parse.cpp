#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <nexenne/serialization/json/parse.hpp>

namespace nexenne::serialization::json {

namespace detail {

parser::parser(std::string_view const src, parse_options const opts) noexcept
    : m_cursor{std::span<char const>{src.data(), src.size()}}, m_opts{opts} {}

auto parser::parse() -> std::expected<value, parse_error> {
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

auto parser::make_error(error const e) const noexcept -> parse_error {
  return {.code = e, .offset = m_cursor.position(), .line = m_line, .column = m_col};
}

auto parser::matches_literal(std::string_view const lit) const noexcept -> bool {
  return m_cursor.has(lit.size()) && std::string_view{m_cursor.data(), lit.size()} == lit;
}

auto parser::advance() noexcept -> char {
  auto const c{m_cursor.next()};
  if (c == '\n') {
    ++m_line;
    m_col = 1;
  } else {
    ++m_col;
  }
  return c;
}

auto parser::skip_ws() noexcept -> void {
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

auto parser::parse_null() -> std::expected<value, parse_error> {
  if (!matches_literal("null")) {
    return std::unexpected{make_error(error::unexpected_character)};
  }
  for (auto i{0}; i < 4; ++i)
    advance();
  return value{};
}

auto parser::parse_bool() -> std::expected<value, parse_error> {
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

auto parser::number_overflows(std::string_view text) noexcept -> bool {
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

auto parser::parse_number() -> std::expected<value, parse_error> {
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
  auto const text{std::string_view{m_cursor.buffer().data() + start, m_cursor.position() - start}};
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

auto parser::parse_string() -> std::expected<std::string, parse_error> {
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

auto parser::parse_array(std::size_t const depth) -> std::expected<value, parse_error> {
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

}  // namespace detail

auto parse(std::string_view const input, parse_options const opts)
  -> std::expected<value, parse_error> {
  auto p{detail::parser{input, opts}};
  return p.parse();
}

}  // namespace nexenne::serialization::json
