#pragma once

/**
 * @file
 * @brief Streaming JSON writer over a caller-provided buffer.
 *
 * Modelled on TinyCBOR's encoder API and ArduinoJson's
 * \c JsonObject::add, every emit op returns
 * \c std::expected<void, error>. The output goes directly into
 * a \c std::span<char> the caller owns:
 *
 *   - On embedded targets, point this at the TX DMA buffer and
 *     ship the result with no copies.
 *   - On hosts, point it at a stack-allocated array, or any
 *     other writable span.
 *
 * The writer enforces structural correctness as you emit:
 * calling \c value() where a key is expected, \c key() where a
 * value is expected, or \c end_object() inside an array (and
 * vice versa) returns \c error::invalid_input.
 *
 * Memory profile:
 *
 *   - No allocation.
 *   - Bounded depth: a \c std::array<bool, MaxDepth> tracks
 *     each open container (object vs array), defaults to 32.
 *   - All numbers go through \c std::to_chars into a small
 *     stack buffer (24 bytes), no \c snprintf, no locale.
 *
 * Errors:
 *
 *   - \c error::buffer_full          - destination exhausted
 *   - \c error::depth_limit_exceeded, too many open containers
 *   - \c error::invalid_input        - structural violation
 *
 * Typical use:
 *
 * \code
 * auto buf{std::array<char, 256>{}};
 * auto w{json::writer{buf}};
 * static_cast<void>(w.begin_object)();
 * static_cast<void>(w.key)("name");  static_cast<void>(w.value)("alice");
 * static_cast<void>(w.key)("age");   static_cast<void>(w.value)(30);
 * static_cast<void>(w.end_object)();
 * auto const out{w.view()};   // std::string_view into buf
 * \endcode
 */

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>

#include <nexenne/serialization/error.hpp>

namespace nexenne::serialization::json {

/**
 * @brief Streaming JSON writer over a caller-provided character span.
 *
 * Emits JSON one token at a time into a \c std::span the caller owns, with
 * no allocation. The writer tracks structural state, so emitting a key
 * where a value is expected (or closing the wrong container) is rejected
 * with \c error::invalid_input rather than producing malformed output.
 * Numbers are formatted with \c std::to_chars; NaN and infinity are
 * written as \c null.
 *
 * @tparam MaxDepth  Maximum container nesting depth. The writer reserves a
 *                   \c std::array<bool, MaxDepth> to track each open
 *                   container; defaults to 32.
 *
 * @note All emit operations are \c noexcept and never allocate.
 */
template <std::size_t MaxDepth = 32>
class writer {
public:
  using size_type = std::size_t;

private:
  enum class slot : std::uint8_t {
    top,               ///< before any top-level value
    top_done,          ///< top-level value emitted; nothing more accepted
    array_first,       ///< first array element pending
    array_next,        ///< subsequent array elements
    object_first_key,  ///< first key in an object
    object_next_key,   ///< subsequent keys
    object_value,      ///< key just written, value expected
  };

  std::span<char> m_buf{};
  size_type m_pos{0};
  slot m_state{slot::top};
  std::array<bool, MaxDepth> m_is_object{};  // true = object, false = array
  size_type m_depth{0};

  [[nodiscard]] constexpr auto fits(size_type const n) const noexcept -> bool {
    return n <= m_buf.size() - m_pos;
  }

  auto raw_put(char const c) noexcept -> std::expected<void, error> {
    if (!fits(1)) [[unlikely]]
      return std::unexpected{error::buffer_full};
    m_buf[m_pos++] = c;
    return {};
  }

  auto raw_write(std::string_view const s) noexcept -> std::expected<void, error> {
    if (!fits(s.size())) [[unlikely]]
      return std::unexpected{error::buffer_full};
    // memcpy with a null pointer is UB even for size 0; an empty string_view's
    // data() may be null.
    if (!s.empty()) {
      std::memcpy(m_buf.data() + m_pos, s.data(), s.size());
    }
    m_pos += s.size();
    return {};
  }

  // Common prefix work for every scalar / container open. Emits
  // the leading comma (if any) and validates context.
  auto begin_value_slot() noexcept -> std::expected<void, error> {
    switch (m_state) {
      case slot::top:
        return {};
      case slot::array_first:
        return {};
      case slot::array_next:
        return raw_put(',');
      case slot::object_value:
        return {};
      case slot::top_done:
      case slot::object_first_key:
      case slot::object_next_key:
        return std::unexpected{error::invalid_input};
    }
    return {};
  }

  // Transition after writing a scalar / closing a container -
  // depends on the slot we were in, and pops back to parent
  // state if a container just closed.
  constexpr auto advance_after_value() noexcept -> void {
    switch (m_state) {
      case slot::top:
        m_state = slot::top_done;
        return;
      case slot::array_first:
        m_state = slot::array_next;
        return;
      case slot::array_next:
        return;
      case slot::object_value:
        m_state = slot::object_next_key;
        return;
      default:
        return;
    }
  }

  constexpr auto pop_container_state() noexcept -> void {
    if (m_depth == 0) {
      m_state = slot::top_done;
      return;
    }
    m_state = m_is_object[m_depth - 1] ? slot::object_value  // parent is object; value just closed
                                       : slot::array_next;   // parent is array; element just closed
    advance_after_value();
  }

  auto write_escaped(std::string_view const s) noexcept -> std::expected<void, error> {
    if (auto r{raw_put('"')}; !r)
      return r;
    for (auto const c : s) {
      auto const uc{static_cast<unsigned char>(c)};
      switch (uc) {
        case '"':
          if (auto r{raw_write("\\\"")}; !r)
            return r;
          continue;
        case '\\':
          if (auto r{raw_write("\\\\")}; !r)
            return r;
          continue;
        case '\b':
          if (auto r{raw_write("\\b")}; !r)
            return r;
          continue;
        case '\f':
          if (auto r{raw_write("\\f")}; !r)
            return r;
          continue;
        case '\n':
          if (auto r{raw_write("\\n")}; !r)
            return r;
          continue;
        case '\r':
          if (auto r{raw_write("\\r")}; !r)
            return r;
          continue;
        case '\t':
          if (auto r{raw_write("\\t")}; !r)
            return r;
          continue;
        default:
          break;
      }
      if (uc < 0x20) {
        // Manual hex, no snprintf or locale (as the file header promises). A
        // control byte is < 0x20, so the form is always "\u00XX".
        constexpr char hex[]{"0123456789abcdef"};
        auto const esc{std::array<char, 6>{'\\', 'u', '0', '0', hex[(uc >> 4) & 0xF], hex[uc & 0xF]}
        };
        if (auto r{raw_write({esc.data(), 6})}; !r)
          return r;
        continue;
      }
      if (auto r{raw_put(c)}; !r)
        return r;
    }
    return raw_put('"');
  }

public:
  /**
   * @brief Construct a writer over the mutable character span \p buf.
   *
   * The writer starts empty at the top-level slot and does not own the
   * backing memory.
   *
   * @param buf  Destination characters to fill. Must outlive the writer.
   *
   * @pre \p buf refers to writable memory for the lifetime of the
   *       writer.
   * @post \c bytes_written() is zero and \c depth() is zero.
   */
  explicit constexpr writer(std::span<char> const buf) noexcept : m_buf{buf} {}

  /**
   * @brief Number of characters emitted so far.
   *
   * @return Current cursor offset.
   *
   * @pre None.
   * @post Result is in the range \c [0, capacity()].
   */
  [[nodiscard]] constexpr auto bytes_written() const noexcept -> size_type {
    return m_pos;
  }

  /**
   * @brief Free space left in the destination span.
   *
   * @return \c capacity() minus \c bytes_written().
   *
   * @pre None.
   * @post Result plus \c bytes_written() equals \c capacity().
   */
  [[nodiscard]] constexpr auto bytes_remaining() const noexcept -> size_type {
    return m_buf.size() - m_pos;
  }

  /**
   * @brief Total size of the destination span.
   *
   * @return Number of characters in the backing span.
   *
   * @pre None.
   * @post Result is constant for the lifetime of the writer.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_buf.size();
  }

  /**
   * @brief Number of currently open containers.
   *
   * @return Count of objects and arrays opened but not yet closed.
   *
   * @pre None.
   * @post Result is in the range \c [0, MaxDepth].
   */
  [[nodiscard]] constexpr auto depth() const noexcept -> size_type {
    return m_depth;
  }

  /**
   * @brief View of the JSON written so far.
   *
   * @return A view of the populated prefix of the buffer.
   *
   * @pre None.
   * @post The returned view has size \c bytes_written().
   *
   * @warning The view aliases the backing span and is invalidated by
   *          any later emit, \c reset, or destruction of the buffer.
   */
  [[nodiscard]] constexpr auto view() const noexcept -> std::string_view {
    return {m_buf.data(), m_pos};
  }

  /**
   * @brief Whether a complete top-level document has been written.
   *
   * @return \c true when a single top-level value was emitted and every
   *         opened container has been closed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_complete() const noexcept -> bool {
    return m_depth == 0 && m_state == slot::top_done;
  }

  /**
   * @brief Rewind to an empty top-level state, reusing the buffer.
   *
   * Discards the structural cursor and position without clearing the
   * underlying characters.
   *
   * @pre None.
   * @post \c bytes_written() and \c depth() are zero and the writer
   *       again expects a top-level value.
   */
  constexpr auto reset() noexcept -> void {
    m_pos = 0;
    m_depth = 0;
    m_state = slot::top;
  }

  /**
   * @brief Open a JSON object where a value is expected.
   *
   * Emits \c '{' (with a leading comma when continuing an array) and
   * pushes an object context. Following calls must emit keys via
   * \c key.
   *
   * @return Empty on success.
   *
   * @pre A value is structurally expected at the current position.
   * @post On success an object context is open and \c depth() has
   *       increased by one; on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a value is not
   *         expected, \c error::depth_limit_exceeded at \c MaxDepth, or
   *         \c error::buffer_full when the brace does not fit.
   */
  auto begin_object() noexcept -> std::expected<void, error> {
    if (auto r{begin_value_slot()}; !r)
      return r;
    if (m_depth >= MaxDepth) [[unlikely]]
      return std::unexpected{error::depth_limit_exceeded};
    if (auto r{raw_put('{')}; !r)
      return r;
    m_is_object[m_depth++] = true;
    m_state = slot::object_first_key;
    return {};
  }

  /**
   * @brief Close the currently open object.
   *
   * Emits \c '}' and pops the object context.
   *
   * @return Empty on success.
   *
   * @pre An object is the innermost open container and a key (not a
   *       dangling value) is expected.
   * @post On success the object is closed and \c depth() has decreased
   *       by one; on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when no object is open
   *         or a value is still pending, or \c error::buffer_full when
   *         the brace does not fit.
   */
  auto end_object() noexcept -> std::expected<void, error> {
    if (m_depth == 0 || !m_is_object[m_depth - 1]) {
      return std::unexpected{error::invalid_input};
    }
    if (m_state != slot::object_first_key && m_state != slot::object_next_key) {
      return std::unexpected{error::invalid_input};
    }
    if (auto r{raw_put('}')}; !r)
      return r;
    --m_depth;
    pop_container_state();
    return {};
  }

  /**
   * @brief Open a JSON array where a value is expected.
   *
   * Emits \c '[' (with a leading comma when continuing an array) and
   * pushes an array context. Following calls emit elements via the
   * \c value overloads or nested containers.
   *
   * @return Empty on success.
   *
   * @pre A value is structurally expected at the current position.
   * @post On success an array context is open and \c depth() has
   *       increased by one; on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a value is not
   *         expected, \c error::depth_limit_exceeded at \c MaxDepth, or
   *         \c error::buffer_full when the bracket does not fit.
   */
  auto begin_array() noexcept -> std::expected<void, error> {
    if (auto r{begin_value_slot()}; !r)
      return r;
    if (m_depth >= MaxDepth) [[unlikely]]
      return std::unexpected{error::depth_limit_exceeded};
    if (auto r{raw_put('[')}; !r)
      return r;
    m_is_object[m_depth++] = false;
    m_state = slot::array_first;
    return {};
  }

  /**
   * @brief Close the currently open array.
   *
   * Emits \c ']' and pops the array context.
   *
   * @return Empty on success.
   *
   * @pre An array is the innermost open container.
   * @post On success the array is closed and \c depth() has decreased by
   *       one; on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when no array is open,
   *         or \c error::buffer_full when the bracket does not fit.
   */
  auto end_array() noexcept -> std::expected<void, error> {
    if (m_depth == 0 || m_is_object[m_depth - 1]) {
      return std::unexpected{error::invalid_input};
    }
    if (m_state != slot::array_first && m_state != slot::array_next) {
      return std::unexpected{error::invalid_input};
    }
    if (auto r{raw_put(']')}; !r)
      return r;
    --m_depth;
    pop_container_state();
    return {};
  }

  /**
   * @brief Emit an object member key.
   *
   * Writes the escaped key and a colon (prefixing a comma between
   * members), then expects a value next.
   *
   * @param k  Member name; escaped as needed and expected to be valid
   *           UTF-8.
   *
   * @return Empty on success.
   *
   * @pre A key is structurally expected (inside an object, not directly
   *       after another key).
   * @post On success the writer expects a value; on failure the writer
   *       state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a key is not
   *         expected, or \c error::buffer_full when the key does not
   *         fit.
   */
  auto key(std::string_view const k) noexcept -> std::expected<void, error> {
    if (m_state == slot::object_next_key) {
      if (auto r{raw_put(',')}; !r)
        return r;
    } else if (m_state != slot::object_first_key) {
      return std::unexpected{error::invalid_input};
    }
    if (auto r{write_escaped(k)}; !r)
      return r;
    if (auto r{raw_put(':')}; !r)
      return r;
    m_state = slot::object_value;
    return {};
  }

  /**
   * @brief Emit a JSON \c null in a value position.
   *
   * @return Empty on success.
   *
   * @pre A value is structurally expected at the current position.
   * @post On success the value slot is filled and the writer advances;
   *       on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a value is not
   *         expected, or \c error::buffer_full when it does not fit.
   */
  auto value_null() noexcept -> std::expected<void, error> {
    if (auto r{begin_value_slot()}; !r)
      return r;
    if (auto r{raw_write("null")}; !r)
      return r;
    advance_after_value();
    return {};
  }

  /**
   * @brief Emit a boolean value.
   *
   * @param b  Boolean to write as \c true or \c false.
   *
   * @return Empty on success.
   *
   * @pre A value is structurally expected at the current position.
   * @post On success the value slot is filled and the writer advances;
   *       on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a value is not
   *         expected, or \c error::buffer_full when it does not fit.
   */
  auto value(bool const b) noexcept -> std::expected<void, error> {
    if (auto r{begin_value_slot()}; !r)
      return r;
    if (auto r{raw_write(b ? "true" : "false")}; !r)
      return r;
    advance_after_value();
    return {};
  }

  /**
   * @brief Emit an integer value.
   *
   * Formatted with \c std::to_chars. The \c bool overload is excluded so
   * booleans take the dedicated overload.
   *
   * @tparam I  Integral type other than \c bool.
   * @param i  Integer to write.
   *
   * @return Empty on success.
   *
   * @pre A value is structurally expected at the current position.
   * @post On success the value slot is filled and the writer advances;
   *       on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a value is not
   *         expected, or \c error::buffer_full when it does not fit.
   */
  template <std::integral I>
    requires(!std::same_as<I, bool>)
  auto value(I const i) noexcept -> std::expected<void, error> {
    if (auto r{begin_value_slot()}; !r)
      return r;
    auto buf{std::array<char, 24>{}};
    auto const r{std::to_chars(buf.data(), buf.data() + buf.size(), i)};
    if (r.ec != std::errc{}) [[unlikely]]
      return std::unexpected{error::buffer_full};
    if (auto w{raw_write({buf.data(), static_cast<size_type>(r.ptr - buf.data())})}; !w)
      return w;
    advance_after_value();
    return {};
  }

  /**
   * @brief Emit a floating-point value.
   *
   * Formatted with \c std::to_chars. NaN and infinity are written as
   * \c null since JSON cannot represent them.
   *
   * @tparam F  Floating-point type.
   * @param f  Value to write.
   *
   * @return Empty on success.
   *
   * @pre A value is structurally expected at the current position.
   * @post On success the value slot is filled and the writer advances;
   *       on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a value is not
   *         expected, or \c error::buffer_full when it does not fit.
   */
  template <std::floating_point F>
  auto value(F const f) noexcept -> std::expected<void, error> {
    if (auto r{begin_value_slot()}; !r)
      return r;
    if (std::isnan(f) || std::isinf(f)) {
      if (auto r{raw_write("null")}; !r)
        return r;
    } else {
      auto buf{std::array<char, 40>{}};
      // Reserve two bytes so a ".0" can always be appended below.
      auto const r{std::to_chars(buf.data(), buf.data() + buf.size() - 2, f)};
      if (r.ec != std::errc{}) [[unlikely]]
        return std::unexpected{error::buffer_full};
      auto len{static_cast<std::size_t>(r.ptr - buf.data())};
      std::string_view const num{buf.data(), len};
      // Keep it a JSON float: to_chars can render a large magnitude in pure
      // integer form (e.g. 31480088169990615040), which would reparse as an
      // integer and overflow. Append ".0" so it stays a floating-point literal.
      if (num.find('.') == std::string_view::npos && num.find('e') == std::string_view::npos
          && num.find('E') == std::string_view::npos) {
        buf[len] = '.';
        buf[len + 1] = '0';
        len += 2;
      }
      if (auto w{raw_write({buf.data(), static_cast<size_type>(len)})}; !w)
        return w;
    }
    advance_after_value();
    return {};
  }

  /**
   * @brief Emit a string value, escaped as JSON requires.
   *
   * @param s  String to write; the JSON-required characters are escaped
   *           and the bytes are expected to be valid UTF-8.
   *
   * @return Empty on success.
   *
   * @pre A value is structurally expected at the current position.
   * @post On success the value slot is filled and the writer advances;
   *       on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a value is not
   *         expected, or \c error::buffer_full when it does not fit.
   */
  auto value(std::string_view const s) noexcept -> std::expected<void, error> {
    if (auto r{begin_value_slot()}; !r)
      return r;
    if (auto r{write_escaped(s)}; !r)
      return r;
    advance_after_value();
    return {};
  }

  /**
   * @brief Emit a string value from a null-terminated C string.
   *
   * Convenience overload that forwards to the \c std::string_view form.
   *
   * @param s  Null-terminated string to write.
   *
   * @return Empty on success.
   *
   * @pre \p s is non-null and null-terminated, and a value is
   *       structurally expected at the current position.
   * @post On success the value slot is filled and the writer advances;
   *       on failure the writer state is unchanged.
   *
   * @throws None. Returns \c error::invalid_input when a value is not
   *         expected, or \c error::buffer_full when it does not fit.
   */
  auto value(char const* const s) noexcept -> std::expected<void, error> {
    return value(std::string_view{s});
  }
};

}  // namespace nexenne::serialization::json
