#pragma once

/**
 * @file
 * @brief Streaming SAX-style JSON parser, zero heap, bounded
 *        stack, suitable for bare-metal MCUs.
 *
 * Inspired by jsmn (the embedded C tokenizer) and nlohmann's
 * \c sax_parse: instead of building a DOM, the parser walks the
 * source once and invokes visitor callbacks for each event:
 *
 *   - \c on_null(),  \c on_bool(b)
 *   - \c on_int(i),  \c on_float(d)
 *   - \c on_string(sv)                 - view into source
 *   - \c on_key(sv)                    - object key, view into source
 *   - \c on_begin_object(),  \c on_end_object()
 *   - \c on_begin_array(),   \c on_end_array()
 *
 * The visitor returns \c bool from every callback, \c false
 * aborts parsing early (useful for streaming filters that only
 * care about a single field). All callbacks default to a no-op
 * via the base \c noop_visitor, so visitors only need to
 * override the events they care about.
 *
 * Memory profile:
 *
 *   - No allocation. Zero. All buffers come from the caller.
 *   - Bounded recursion: \c parse_value descends one call frame
 *     per nested array or object and refuses to descend past
 *     \c MaxDepth (an NTTP, default 32). Size it to suit the
 *     target's stack budget.
 *   - Strings are returned as \c std::string_view into the
 *     source buffer. Escape sequences are validated but NOT
 *     decoded in place, to avoid a scratch buffer; if you need
 *     decoded text (or duplicate-key rejection), use the DOM
 *     parser in parse.hpp instead.
 *
 * Number policy: integers with no fractional/exponent part are
 * delivered to \c on_int; everything else hits \c on_float. A
 * magnitude too small for a \c double underflows to a signed
 * zero, while one too large is rejected with
 * \c error::invalid_number rather than delivered as infinity.
 *
 * Errors:
 *
 *   - \c error::unexpected_character / \c error::unexpected_end
 *   - \c error::depth_limit_exceeded, bumped \c MaxDepth
 *   - \c error::invalid_input        - visitor returned false
 *   - \c error::invalid_number, \c error::invalid_escape, etc.
 */

#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include <nexenne/serialization/error.hpp>
#include <nexenne/utility/buffer_cursor.hpp>

namespace nexenne::serialization::json {

/**
 * @brief Requirements a type must meet to drive \c scan.
 *
 * A visitor must expose all ten event callbacks, each taking the event's
 * payload and returning \c bool. Returning \c false from any callback
 * aborts the parse cleanly, which \c scan surfaces as
 * \c error::invalid_input.
 *
 * @tparam V  Candidate visitor type.
 */
template <typename V>
concept sax_visitor = requires(V& v, bool b, std::int64_t i, double d, std::string_view sv) {
  { v.on_null() } -> std::same_as<bool>;
  { v.on_bool(b) } -> std::same_as<bool>;
  { v.on_int(i) } -> std::same_as<bool>;
  { v.on_float(d) } -> std::same_as<bool>;
  { v.on_string(sv) } -> std::same_as<bool>;
  { v.on_key(sv) } -> std::same_as<bool>;
  { v.on_begin_object() } -> std::same_as<bool>;
  { v.on_end_object() } -> std::same_as<bool>;
  { v.on_begin_array() } -> std::same_as<bool>;
  { v.on_end_array() } -> std::same_as<bool>;
};

/**
 * @brief Default-implemented visitor. Inherit and override the
 *        events you care about; the rest stay no-ops.
 */
struct noop_visitor {
  /**
   * @brief Handle a JSON null. Default: ignore and continue.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_null() noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle a JSON boolean. Default: ignore and continue.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_bool(bool) noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle an integer number. Default: ignore and continue.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_int(std::int64_t) noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle a floating-point number. Default: ignore and continue.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_float(double) noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle a string value. Default: ignore and continue.
   *
   * The argument is a raw, undecoded view into the source buffer.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_string(std::string_view) noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle an object key. Default: ignore and continue.
   *
   * The argument is a raw, undecoded view into the source buffer.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_key(std::string_view) noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle the start of an object. Default: ignore and continue.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_begin_object() noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle the end of an object. Default: ignore and continue.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_end_object() noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle the start of an array. Default: ignore and continue.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_begin_array() noexcept -> bool {
    return true;
  }

  /**
   * @brief Handle the end of an array. Default: ignore and continue.
   *
   * @return Always \c true (continue parsing).
   *
   * @pre None.
   * @post None.
   */
  auto on_end_array() noexcept -> bool {
    return true;
  }
};

/// @cond INTERNAL
namespace detail {

/**
 * @brief Recursive-descent SAX engine driving the public \c scan.
 *
 * @tparam MaxDepth Maximum container nesting depth.
 */
template <std::size_t MaxDepth>
class sax_engine {
public:
  using size_type = std::size_t;

private:
  utility::buffer_cursor<char const> m_cursor;
  size_type m_depth{0};

  /**
   * @brief Whether the cursor has reached the end of the source.
   *
   * @return \c true when no bytes remain.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto eof() const noexcept -> bool {
    return m_cursor.exhausted();
  }

  /**
   * @brief Skip whitespace at the cursor.
   *
   * @pre None.
   * @post The cursor sits on the next non-whitespace byte or at end of input.
   */
  constexpr auto skip_ws() noexcept -> void {
    while (!m_cursor.exhausted()) {
      auto const c{m_cursor.data()[0]};
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        m_cursor.advance(1);
      else
        break;
    }
  }

  /**
   * @brief Read four hex digits at the cursor into \p out.
   *
   * @param out Receives the 16-bit value the four digits encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by four bytes; on failure it may have
   *       advanced over part of the run.
   *
   * @throws None. Returns \c error::invalid_escape on a non-hex digit or a run
   *         shorter than four bytes.
   */
  [[nodiscard]] auto read_hex4(std::uint32_t& out) noexcept -> std::expected<void, error> {
    if (!m_cursor.has(4))
      return std::unexpected{error::invalid_escape};
    auto cp{std::uint32_t{0}};
    for (auto i{0}; i < 4; ++i) {
      auto const h{m_cursor.data()[0]};
      m_cursor.advance(1);
      cp <<= 4;
      if (h >= '0' && h <= '9')
        cp |= static_cast<std::uint32_t>(h - '0');
      else if (h >= 'a' && h <= 'f')
        cp |= static_cast<std::uint32_t>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F')
        cp |= static_cast<std::uint32_t>(h - 'A' + 10);
      else
        return std::unexpected{error::invalid_escape};
    }
    out = cp;
    return {};
  }

  /**
   * @brief Validate (without decoding) a \c \\u escape at the cursor.
   *
   * Consumes \c \\uXXXX, plus a paired \c \\uYYYY low surrogate when the first
   * is a high surrogate, and rejects non-hex digits and unpaired surrogates
   * exactly as the DOM parser does so both accept the same string grammar.
   *
   * @return Empty on success.
   *
   * @pre The cursor is at the backslash of a \c \\u escape, with \c has(2)
   *       already confirmed by the caller.
   * @post On success the cursor sits just past the escape (and any paired low
   *       surrogate); on failure it may have advanced over part of it.
   *
   * @throws None. Returns \c error::invalid_escape on a malformed or unpaired
   *         escape.
   */
  [[nodiscard]] auto validate_u_escape() noexcept -> std::expected<void, error> {
    m_cursor.advance(2);  // consume the "\u"; the caller confirmed has(2)
    auto hi{std::uint32_t{0}};
    if (auto const r{read_hex4(hi)}; !r)
      return r;
    if (hi >= 0xD800 && hi <= 0xDBFF) {
      if (!m_cursor.has(6) || m_cursor.data()[0] != '\\' || m_cursor.data()[1] != 'u')
        return std::unexpected{error::invalid_escape};
      m_cursor.advance(2);
      auto lo{std::uint32_t{0}};
      if (auto const r{read_hex4(lo)}; !r)
        return r;
      if (lo < 0xDC00 || lo > 0xDFFF)
        return std::unexpected{error::invalid_escape};
    } else if (hi >= 0xDC00 && hi <= 0xDFFF) {
      return std::unexpected{error::invalid_escape};  // lone low surrogate
    }
    return {};
  }

  /**
   * @brief Scan a JSON string literal, returning a raw view of its body.
   *
   * The returned view covers the source bytes between the quotes. Escapes are
   * validated but NOT decoded, so a visitor that needs decoded text must
   * decode them, or use the DOM parser.
   *
   * @return A view of the raw string body on success.
   *
   * @pre The cursor is at the opening double quote.
   * @post On success the cursor sits just past the closing quote.
   *
   * @throws None. Returns \c error::unexpected_character when not at a quote,
   *         \c error::invalid_escape or \c error::invalid_string on a bad
   *         body, or \c error::unexpected_end when the quote never closes.
   *
   * @warning The returned view aliases the source and is invalidated when the
   *          source buffer is destroyed or modified.
   */
  [[nodiscard]] auto scan_string_raw() noexcept -> std::expected<std::string_view, error> {
    if (eof() || m_cursor.data()[0] != '"')
      return std::unexpected{error::unexpected_character};
    m_cursor.advance(1);
    auto const start{m_cursor.position()};
    while (!m_cursor.exhausted()) {
      auto const c{m_cursor.data()[0]};
      if (c == '"') {
        auto const sv{
          std::string_view{m_cursor.buffer().data() + start, m_cursor.position() - start}
        };
        m_cursor.advance(1);
        return sv;
      }
      if (c == '\\') {
        if (!m_cursor.has(2))
          return std::unexpected{error::invalid_escape};
        auto const esc{m_cursor.data()[1]};
        switch (esc) {
          case '"':
          case '\\':
          case '/':
          case 'b':
          case 'f':
          case 'n':
          case 'r':
          case 't':
            m_cursor.advance(2);
            continue;
          case 'u':
            if (auto const r{validate_u_escape()}; !r)
              return std::unexpected{r.error()};
            continue;
          default:
            return std::unexpected{error::invalid_escape};
        }
      }
      if (static_cast<unsigned char>(c) < 0x20)
        return std::unexpected{error::invalid_string};
      m_cursor.advance(1);
    }
    return std::unexpected{error::unexpected_end};
  }

  /**
   * @brief Whether the source at the cursor begins with keyword \p lit.
   *
   * Mirrors the old \c substr(pos, len) == lit test: a clamped substring can
   * never equal \p lit unless \c lit.size() bytes remain.
   *
   * @param lit Keyword literal to match (for example \c "true").
   *
   * @return \c true when the next bytes equal \p lit.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto matches_literal(std::string_view const lit) const noexcept -> bool {
    return m_cursor.has(lit.size()) && std::string_view{m_cursor.data(), lit.size()} == lit;
  }

public:
  /**
   * @brief Construct an engine over the source text \p src.
   *
   * @param src JSON text to scan. Must outlive the engine.
   *
   * @pre \p src refers to valid characters for the lifetime of the engine.
   * @post The cursor is at offset zero and the depth is zero.
   */
  explicit constexpr sax_engine(std::string_view const src) noexcept
      : m_cursor{std::span<char const>{src.data(), src.size()}} {}

  /**
   * @brief Scan the whole source, driving visitor \p v.
   *
   * Parses one top-level value and requires the rest to be whitespace.
   *
   * @tparam V Visitor type satisfying \c sax_visitor.
   * @param v Visitor receiving the parse events.
   *
   * @return Empty on success, or an error on the first failure.
   *
   * @pre None.
   * @post On success the whole source has been consumed.
   */
  template <sax_visitor V>
  [[nodiscard]] auto run(V& v) noexcept -> std::expected<void, error> {
    skip_ws();
    if (auto r{parse_value(v)}; !r)
      return r;
    skip_ws();
    if (!eof())
      return std::unexpected{error::unexpected_character};
    return {};
  }

private:
  /**
   * @brief Parse one JSON value, dispatching on the leading character.
   *
   * @tparam V Visitor type satisfying \c sax_visitor.
   * @param v Visitor receiving the value event.
   *
   * @return Empty on success, or an error on failure.
   *
   * @pre None.
   * @post On success the cursor sits just past the value and the visitor has
   *       observed its events.
   */
  template <sax_visitor V>
  [[nodiscard]] auto parse_value(V& v) noexcept -> std::expected<void, error> {
    skip_ws();
    if (eof())
      return std::unexpected{error::unexpected_end};
    auto const c{m_cursor.data()[0]};
    switch (c) {
      case '{':
        return parse_object(v);
      case '[':
        return parse_array(v);
      case '"': {
        auto sv{scan_string_raw()};
        if (!sv)
          return std::unexpected{sv.error()};
        if (!v.on_string(*sv))
          return std::unexpected{error::invalid_input};
        return {};
      }
      case 't':
        if (!matches_literal("true"))
          return std::unexpected{error::unexpected_character};
        m_cursor.advance(4);
        if (!v.on_bool(true))
          return std::unexpected{error::invalid_input};
        return {};
      case 'f':
        if (!matches_literal("false"))
          return std::unexpected{error::unexpected_character};
        m_cursor.advance(5);
        if (!v.on_bool(false))
          return std::unexpected{error::invalid_input};
        return {};
      case 'n':
        if (!matches_literal("null"))
          return std::unexpected{error::unexpected_character};
        m_cursor.advance(4);
        if (!v.on_null())
          return std::unexpected{error::invalid_input};
        return {};
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
        return parse_number(v);
      default:
        return std::unexpected{error::unexpected_character};
    }
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
   * @brief Parse a JSON number and deliver it to the visitor.
   *
   * Validates the RFC 8259 grammar, then converts with \c std::from_chars: a
   * literal with no fraction or exponent is delivered to \c on_int (widened to
   * \c on_float when it exceeds the integer range), otherwise to \c on_float.
   * A magnitude too small for a \c double underflows to a signed zero, while
   * one too large is rejected with \c error::invalid_number.
   *
   * @tparam V Visitor type satisfying \c sax_visitor.
   * @param v Visitor receiving the number event.
   *
   * @return Empty on success, or an error on failure.
   *
   * @pre The cursor is at a digit or a leading minus sign.
   * @post On success the cursor sits just past the number literal.
   */
  template <sax_visitor V>
  [[nodiscard]] auto parse_number(V& v) noexcept -> std::expected<void, error> {
    auto const start{m_cursor.position()};
    auto const is_digit{[this] {
      return !m_cursor.exhausted() && m_cursor.data()[0] >= '0' && m_cursor.data()[0] <= '9';
    }};
    if (m_cursor.data()[0] == '-')
      m_cursor.advance(1);
    // Integer part (RFC 8259): a single 0, or [1-9][0-9]*. No leading zeros.
    if (!is_digit())
      return std::unexpected{error::invalid_number};
    if (m_cursor.data()[0] == '0') {
      m_cursor.advance(1);
      if (is_digit())  // a leading zero like "01"
        return std::unexpected{error::invalid_number};
    } else {
      while (is_digit())
        m_cursor.advance(1);
    }
    auto is_float{false};
    if (!m_cursor.exhausted() && m_cursor.data()[0] == '.') {
      is_float = true;
      m_cursor.advance(1);
      if (!is_digit())  // a fraction needs at least one digit
        return std::unexpected{error::invalid_number};
      while (is_digit())
        m_cursor.advance(1);
    }
    if (!m_cursor.exhausted() && (m_cursor.data()[0] == 'e' || m_cursor.data()[0] == 'E')) {
      is_float = true;
      m_cursor.advance(1);
      if (!m_cursor.exhausted() && (m_cursor.data()[0] == '+' || m_cursor.data()[0] == '-'))
        m_cursor.advance(1);
      if (!is_digit())  // an exponent needs at least one digit
        return std::unexpected{error::invalid_number};
      while (is_digit())
        m_cursor.advance(1);
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
        // delivered as infinity. Matches the DOM parser.
        if (number_overflows(text)) {
          return std::unexpected{error::invalid_number};
        }
        if (!v.on_float(text.front() == '-' ? -0.0 : 0.0))
          return std::unexpected{error::invalid_input};
        return {};
      }
      if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) {
        return std::unexpected{error::invalid_number};
      }
      if (!v.on_float(out))
        return std::unexpected{error::invalid_input};
      return {};
    }
    auto out{std::int64_t{0}};
    auto const r{std::from_chars(text.data(), text.data() + text.size(), out)};
    if (r.ec == std::errc::result_out_of_range) {
      // An integer literal beyond int64 range is still a valid JSON number;
      // widen it to double rather than rejecting it.
      auto wide{0.0};
      auto const fr{std::from_chars(text.data(), text.data() + text.size(), wide)};
      if (fr.ec != std::errc{} || fr.ptr != text.data() + text.size()) {
        return std::unexpected{error::invalid_number};
      }
      if (!v.on_float(wide))
        return std::unexpected{error::invalid_input};
      return {};
    }
    if (r.ec != std::errc{} || r.ptr != text.data() + text.size()) {
      return std::unexpected{error::invalid_number};
    }
    if (!v.on_int(out))
      return std::unexpected{error::invalid_input};
    return {};
  }

  /**
   * @brief Parse a JSON array, emitting begin / end array events.
   *
   * Enforces \c MaxDepth on entry and recurses one call frame per element.
   *
   * @tparam V Visitor type satisfying \c sax_visitor.
   * @param v Visitor receiving the array events.
   *
   * @return Empty on success, or an error on failure.
   *
   * @pre The cursor is at the opening bracket.
   * @post On success the cursor sits just past the closing bracket.
   */
  template <sax_visitor V>
  [[nodiscard]] auto parse_array(V& v) noexcept -> std::expected<void, error> {
    if (m_depth >= MaxDepth)
      return std::unexpected{error::depth_limit_exceeded};
    ++m_depth;
    m_cursor.advance(1);  // '['
    if (!v.on_begin_array())
      return std::unexpected{error::invalid_input};
    skip_ws();
    if (!m_cursor.exhausted() && m_cursor.data()[0] == ']') {
      m_cursor.advance(1);
      --m_depth;
      if (!v.on_end_array())
        return std::unexpected{error::invalid_input};
      return {};
    }
    while (true) {
      if (auto r{parse_value(v)}; !r)
        return r;
      skip_ws();
      if (eof())
        return std::unexpected{error::unexpected_end};
      if (m_cursor.data()[0] == ',') {
        m_cursor.advance(1);
        continue;
      }
      if (m_cursor.data()[0] == ']') {
        m_cursor.advance(1);
        --m_depth;
        if (!v.on_end_array())
          return std::unexpected{error::invalid_input};
        return {};
      }
      return std::unexpected{error::unexpected_character};
    }
  }

  /**
   * @brief Parse a JSON object, emitting begin / end object and key events.
   *
   * Enforces \c MaxDepth on entry and recurses one call frame per member
   * value. Keys are delivered as raw, undecoded views.
   *
   * @tparam V Visitor type satisfying \c sax_visitor.
   * @param v Visitor receiving the object events.
   *
   * @return Empty on success, or an error on failure.
   *
   * @pre The cursor is at the opening brace.
   * @post On success the cursor sits just past the closing brace.
   */
  template <sax_visitor V>
  [[nodiscard]] auto parse_object(V& v) noexcept -> std::expected<void, error> {
    if (m_depth >= MaxDepth)
      return std::unexpected{error::depth_limit_exceeded};
    ++m_depth;
    m_cursor.advance(1);  // '{'
    if (!v.on_begin_object())
      return std::unexpected{error::invalid_input};
    skip_ws();
    if (!m_cursor.exhausted() && m_cursor.data()[0] == '}') {
      m_cursor.advance(1);
      --m_depth;
      if (!v.on_end_object())
        return std::unexpected{error::invalid_input};
      return {};
    }
    while (true) {
      skip_ws();
      auto key{scan_string_raw()};
      if (!key)
        return std::unexpected{key.error()};
      if (!v.on_key(*key))
        return std::unexpected{error::invalid_input};
      skip_ws();
      if (eof() || m_cursor.data()[0] != ':')
        return std::unexpected{error::unexpected_character};
      m_cursor.advance(1);
      if (auto r{parse_value(v)}; !r)
        return r;
      skip_ws();
      if (eof())
        return std::unexpected{error::unexpected_end};
      if (m_cursor.data()[0] == ',') {
        m_cursor.advance(1);
        continue;
      }
      if (m_cursor.data()[0] == '}') {
        m_cursor.advance(1);
        --m_depth;
        if (!v.on_end_object())
          return std::unexpected{error::invalid_input};
        return {};
      }
      return std::unexpected{error::unexpected_character};
    }
  }
};

}  // namespace detail
/// @endcond

/**
 * @brief Run the SAX parser over \p src, invoking events on \p visitor.
 *
 * Walks the source once, calling the visitor's callbacks for each JSON
 * event. No allocation occurs: string and key payloads are passed as raw
 * views into \p src and escape sequences are not decoded.
 *
 * @tparam MaxDepth Maximum nesting depth; the parser recurses one call
 *                  frame per level and refuses to descend past \c MaxDepth
 *                  (32 by default). Each level is a real stack frame, so
 *                  size it against the target's stack budget: bump for
 *                  deeply nested payloads, shrink for tight RAM.
 * @tparam V        Visitor type satisfying \c sax_visitor.
 * @param src      JSON text to scan.
 * @param visitor  Receiver of parse events; mutated through its
 *                  callbacks.
 *
 * @return Empty on success, or an error on the first malformed token,
 *         depth overflow, or visitor abort.
 *
 * @pre None.
 * @post On success the whole of \p src has been consumed; the visitor has
 *       observed every event in document order.
 *
 * @throws None from \c scan itself. May propagate an exception only if a
 *         visitor callback throws, which would also break the callbacks'
 *         \c noexcept contract.
 */
template <std::size_t MaxDepth = 32, sax_visitor V>
[[nodiscard]] auto
scan(std::string_view const src, V& visitor) noexcept -> std::expected<void, error> {
  detail::sax_engine<MaxDepth> engine{src};
  return engine.run(visitor);
}

}  // namespace nexenne::serialization::json
