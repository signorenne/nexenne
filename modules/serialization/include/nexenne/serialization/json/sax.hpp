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
 *     source buffer. Escape sequences are NOT decoded in place
 *     to avoid a scratch buffer; if you need decoded text, pass
 *     a writable scratch span via \c scan_with_scratch.
 *
 * Number policy: integers with no fractional/exponent part are
 * delivered to \c on_int; everything else hits \c on_float.
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

namespace detail {

template <std::size_t MaxDepth>
class sax_engine {
public:
  using size_type = std::size_t;

private:
  utility::buffer_cursor<char const> m_cursor;
  size_type m_depth{0};

  [[nodiscard]] constexpr auto eof() const noexcept -> bool {
    return m_cursor.exhausted();
  }

  constexpr auto skip_ws() noexcept -> void {
    while (!m_cursor.exhausted()) {
      auto const c{m_cursor.data()[0]};
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        m_cursor.advance(1);
      else
        break;
    }
  }

  // Scan a JSON string literal at the cursor starting with '"'
  // and return a view into the source covering the unescaped raw
  // body (between the quotes). Does NOT decode escapes, the
  // visitor handles that, or the caller uses scan_with_scratch.
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
        m_cursor.advance(2);
        continue;
      }
      if (static_cast<unsigned char>(c) < 0x20)
        return std::unexpected{error::invalid_string};
      m_cursor.advance(1);
    }
    return std::unexpected{error::unexpected_end};
  }

  // True when the source at the cursor begins with the keyword literal
  // \p lit (e.g. "true"). Mirrors the old substr(pos, len) == lit test:
  // a clamped substr can never equal lit unless len bytes remain.
  [[nodiscard]] auto matches_literal(std::string_view const lit) const noexcept -> bool {
    return m_cursor.has(lit.size()) && std::string_view{m_cursor.data(), lit.size()} == lit;
  }

public:
  explicit constexpr sax_engine(std::string_view const src) noexcept
      : m_cursor{std::span<char const>{src.data(), src.size()}} {}

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
