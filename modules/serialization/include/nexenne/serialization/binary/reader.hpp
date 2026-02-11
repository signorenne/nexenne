#pragma once

/**
 * @file
 * @brief Schema-driven binary reader, symmetric to \c writer.
 *
 * Reads from a caller-provided immutable byte span. Decoding is
 * zero-copy where possible: \c read_string() returns a
 * \c std::string_view directly into the source buffer.
 *
 * The reader walks the same schema the writer wrote, so the
 * sequence of \c read calls on the consumer side must mirror
 * the producer's \c write calls.
 *
 * Heap-free, exception-free, and \c noexcept end to end. Suitable
 * for parsing a CAN/UART/BLE frame in place inside an MCU ISR.
 *
 * All reads return \c std::expected<T, error>:
 *
 *   - \c error::buffer_underrun when the source ran out of bytes
 *     mid-value.
 *   - \c error::string_too_long when a string length prefix
 *     exceeds the safety cap (defaults to 64 MiB; override with
 *     \c set_max_string_size on memory-constrained targets).
 *
 * Typical use:
 *
 * \code
 * auto r{reader{written_span}};
 * auto const id   {*r.read<std::uint32_t>()};
 * auto const pi   {*r.read<double>()};
 * auto const name {*r.read_string()};
 * \endcode
 */

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

#include <nexenne/serialization/error.hpp>

namespace nexenne::serialization::binary {

/**
 * @brief Schema-driven, zero-copy binary reader over an immutable span.
 *
 * Walks a caller-provided \c std::span of \c const bytes, decoding the
 * little-endian payload a \c writer produced. The read sequence must
 * mirror the producer's write sequence because the format carries no
 * type tags. \c read_string and \c read_bytes return views directly into
 * the source buffer, so no allocation or copy happens on the hot path.
 *
 * Every decode is bounds-checked and reports failure through
 * \c std::expected rather than throwing, which keeps the class usable
 * inside an interrupt handler on a freestanding target.
 *
 * @note All methods are \c noexcept and never allocate.
 */
class reader {
public:
  using byte_type = std::byte;
  using size_type = std::size_t;

  /**
   * @brief Default hard cap on the length of a single decoded string.
   *
   * Defends against corrupt or malicious input that claims a
   * gigabyte-long string. Override per instance with
   * \c set_max_string_size for tight RAM budgets (e.g. 256 on a
   * Cortex-M0). Value is 64 MiB.
   */
  static constexpr size_type default_max_string_size{64u * 1024u * 1024u};

private:
  std::span<byte_type const> m_buf{};
  size_type m_pos{0};
  size_type m_max_str{default_max_string_size};

  [[nodiscard]] constexpr auto has(size_type const n) const noexcept -> bool {
    return n <= m_buf.size() - m_pos;
  }

public:
  /**
   * @brief Construct a reader over the immutable byte span \p buf.
   *
   * The cursor starts at offset zero. The reader stores the span by
   * value but does not own the underlying bytes.
   *
   * @param buf  Source bytes to decode. Must outlive the reader and any
   *             view it returns.
   *
   * @pre \p buf refers to valid memory for the lifetime of the reader.
   * @post \c position() is zero and \c bytes_remaining() equals
   *       \c buf.size().
   */
  explicit constexpr reader(std::span<byte_type const> const buf) noexcept : m_buf{buf} {}

  /**
   * @brief Number of bytes consumed so far.
   *
   * @return Current cursor offset from the start of the buffer.
   *
   * @pre None.
   * @post Result is in the range \c [0, capacity()].
   */
  [[nodiscard]] constexpr auto bytes_read() const noexcept -> size_type {
    return m_pos;
  }

  /**
   * @brief Number of bytes left to read.
   *
   * @return \c capacity() minus \c position().
   *
   * @pre None.
   * @post Result plus \c bytes_read() equals \c capacity().
   */
  [[nodiscard]] constexpr auto bytes_remaining() const noexcept -> size_type {
    return m_buf.size() - m_pos;
  }

  /**
   * @brief Current cursor offset.
   *
   * @return Offset of the next byte to be read; identical to
   *         \c bytes_read().
   *
   * @pre None.
   * @post Result is in the range \c [0, capacity()].
   */
  [[nodiscard]] constexpr auto position() const noexcept -> size_type {
    return m_pos;
  }

  /**
   * @brief Total size of the source buffer.
   *
   * @return Number of bytes in the span the reader was constructed with.
   *
   * @pre None.
   * @post Result is constant for the lifetime of the reader.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_buf.size();
  }

  /**
   * @brief Whether the cursor has reached the end of the buffer.
   *
   * @return \c true when no bytes remain to be read.
   *
   * @pre None.
   * @post Result equals \c (bytes_remaining() == 0).
   */
  [[nodiscard]] constexpr auto at_end() const noexcept -> bool {
    return m_pos == m_buf.size();
  }

  /**
   * @brief Override the per-string size cap for this reader.
   *
   * Subsequent \c read_string calls fail with \c error::string_too_long
   * when the decoded length prefix exceeds \p n. Lower it on
   * RAM-constrained targets.
   *
   * @param n  New maximum decoded string length, in bytes.
   *
   * @pre None.
   * @post Later \c read_string calls reject lengths greater than \p n.
   */
  constexpr auto set_max_string_size(size_type const n) noexcept -> void {
    m_max_str = n;
  }

  /**
   * @brief Move the cursor to absolute offset \p pos.
   *
   * Useful for random access into a known layout, such as a flash block
   * whose field offsets the caller already knows.
   *
   * @param pos  Absolute offset to seek to, in bytes from the start.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c position() equals \p pos; on failure the cursor
   *       is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun when \p pos exceeds
   *         \c capacity().
   */
  auto seek(size_type const pos) noexcept -> std::expected<void, error> {
    if (pos > m_buf.size()) [[unlikely]]
      return std::unexpected{error::buffer_underrun};
    m_pos = pos;
    return {};
  }

  /**
   * @brief Advance the cursor by \p n bytes without decoding them.
   *
   * Steps over a padding region or an unknown field whose size the
   * schema specifies. Bounds-checked.
   *
   * @param n  Number of bytes to skip.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c position() increases by \p n; on failure the
   *       cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun when fewer than
   *         \p n bytes remain.
   */
  auto skip(size_type const n) noexcept -> std::expected<void, error> {
    if (!has(n)) [[unlikely]]
      return std::unexpected{error::buffer_underrun};
    m_pos += n;
    return {};
  }

  /**
   * @brief Decode one trivially-copyable primitive from little-endian
   *        bytes and advance the cursor.
   *
   * On the little-endian fast path the read is a single \c memcpy; on a
   * big-endian host the bytes are reversed first. Works for any built-in
   * integer or floating-point type.
   *
   * @tparam T  Trivially-copyable integral or floating-point type.
   *
   * @return The decoded value on success.
   *
   * @pre None.
   * @post On success \c position() increases by \c sizeof(T); on failure
   *       the cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun when fewer than
   *         \c sizeof(T) bytes remain.
   */
  template <typename T>
    requires std::is_trivially_copyable_v<T> && (std::integral<T> || std::floating_point<T>)
  [[nodiscard]] auto read() noexcept -> std::expected<T, error> {
    if (!has(sizeof(T))) [[unlikely]]
      return std::unexpected{error::buffer_underrun};
    auto value{T{}};
    if constexpr (std::endian::native == std::endian::little) {
      std::memcpy(&value, m_buf.data() + m_pos, sizeof(T));
    } else {
      // Cold path, every supported MCU target is little-endian.
      auto bytes{std::array<byte_type, sizeof(T)>{}};
      for (size_type i{0}; i < sizeof(T); ++i) {
        bytes[sizeof(T) - 1 - i] = m_buf[m_pos + i];
      }
      std::memcpy(&value, bytes.data(), sizeof(T));
    }
    m_pos += sizeof(T);
    return value;
  }

  /**
   * @brief Decode one primitive without advancing the cursor.
   *
   * Look-ahead for branch-on-tag protocols where the next decode path
   * depends on an upcoming value. Decodes exactly as \c read but leaves
   * the cursor in place.
   *
   * @tparam T  Trivially-copyable integral or floating-point type.
   *
   * @return The value at the current position on success.
   *
   * @pre None.
   * @post The cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun when fewer than
   *         \c sizeof(T) bytes remain.
   */
  template <typename T>
    requires std::is_trivially_copyable_v<T> && (std::integral<T> || std::floating_point<T>)
  [[nodiscard]] auto peek() const noexcept -> std::expected<T, error> {
    if (!has(sizeof(T))) [[unlikely]]
      return std::unexpected{error::buffer_underrun};
    auto value{T{}};
    if constexpr (std::endian::native == std::endian::little) {
      std::memcpy(&value, m_buf.data() + m_pos, sizeof(T));
    } else {
      auto bytes{std::array<byte_type, sizeof(T)>{}};
      for (size_type i{0}; i < sizeof(T); ++i) {
        bytes[sizeof(T) - 1 - i] = m_buf[m_pos + i];
      }
      std::memcpy(&value, bytes.data(), sizeof(T));
    }
    return value;
  }

  /**
   * @brief Read \p n raw bytes as a view into the source buffer.
   *
   * Zero-copy: the returned span aliases the reader's backing storage
   * and stays valid only as long as that storage does.
   *
   * @param n  Number of bytes to view.
   *
   * @return A span covering the next \p n bytes on success.
   *
   * @pre None.
   * @post On success \c position() increases by \p n and the returned
   *       span has size \p n; on failure the cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun when fewer than
   *         \p n bytes remain.
   *
   * @warning The returned span is invalidated when the source buffer is
   *          destroyed or modified.
   */
  [[nodiscard]] auto read_bytes(size_type const n
  ) noexcept -> std::expected<std::span<byte_type const>, error> {
    if (!has(n)) [[unlikely]]
      return std::unexpected{error::buffer_underrun};
    auto const out{m_buf.subspan(m_pos, n)};
    m_pos += n;
    return out;
  }

  /**
   * @brief Bulk-read \c out.size() primitives into \p out.
   *
   * One \c memcpy on little-endian hosts (or whenever \c sizeof(T) is 1);
   * otherwise each element is byte-swapped through \c read.
   *
   * @tparam T  Trivially-copyable integral or floating-point type.
   * @param out  Destination span; its current size sets the element
   *              count to read.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c position() increases by \c out.size()*sizeof(T)
   *       and every element of \p out is overwritten; on failure the
   *       cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun when fewer than
   *         \c out.size()*sizeof(T) bytes remain.
   */
  template <typename T>
    requires std::is_trivially_copyable_v<T> && (std::integral<T> || std::floating_point<T>)
  auto read_array(std::span<T> const out) noexcept -> std::expected<void, error> {
    // Compare counts, not byte totals: out.size() * sizeof(T) could overflow
    // size_t and wrap to a small value that passes a bounds check, then memcpy
    // would over-write the destination span.
    if (out.size() > (m_buf.size() - m_pos) / sizeof(T)) [[unlikely]] {
      return std::unexpected{error::buffer_underrun};
    }
    auto const n{out.size() * sizeof(T)};
    if constexpr (std::endian::native == std::endian::little || sizeof(T) == 1) {
      std::memcpy(out.data(), m_buf.data() + m_pos, n);
      m_pos += n;
    } else {
      for (auto& x : out) {
        x = *read<T>();
      }
    }
    return {};
  }

  /**
   * @brief Decode an unsigned LEB128 varint and advance the cursor.
   *
   * Reads up to 10 bytes, each contributing 7 bits of magnitude with
   * the high bit signalling continuation, matching
   * \c writer::write_varint.
   *
   * @return The decoded value on success.
   *
   * @pre None.
   * @post On success \c position() advances by the number of bytes the
   *       varint occupied (1 to 10); on failure the cursor may have
   *       advanced over the consumed continuation bytes.
   *
   * @throws None. Returns \c error::buffer_underrun when the buffer ends
   *         mid-varint, or \c error::invalid_input when the encoding
   *         exceeds 10 bytes (over-long for a 64-bit value).
   */
  [[nodiscard]] auto read_varint() noexcept -> std::expected<std::uint64_t, error> {
    auto value{std::uint64_t{0}};
    auto shift{0};
    // Max 10 bytes for a 64-bit value.
    for (auto i{0}; i < 10; ++i) {
      if (!has(1)) [[unlikely]]
        return std::unexpected{error::buffer_underrun};
      auto const b{static_cast<std::uint8_t>(m_buf[m_pos++])};
      // The 10th byte holds only bit 63, so its value bits beyond bit 0 would
      // overflow uint64: reject a non-canonical / over-wide encoding.
      if (i == 9 && (b & 0x7F) > 0x01u) [[unlikely]] {
        return std::unexpected{error::invalid_input};
      }
      value |= static_cast<std::uint64_t>(b & 0x7F) << shift;
      if ((b & 0x80) == 0) {
        return value;
      }
      shift += 7;
    }
    return std::unexpected{error::invalid_input};
  }

  /**
   * @brief Decode a zigzag plus LEB128 encoded signed integer.
   *
   * Reads the underlying varint, then maps it back from zigzag so that
   * small magnitudes of either sign cost few bytes. Mirrors
   * \c writer::write_zigzag.
   *
   * @return The decoded signed value on success.
   *
   * @pre None.
   * @post On success the cursor advances past the underlying varint; on
   *       failure the cursor reflects however far the varint decode got.
   *
   * @throws None. Propagates \c error::buffer_underrun or
   *         \c error::invalid_input from the underlying varint decode.
   */
  [[nodiscard]] auto read_zigzag() noexcept -> std::expected<std::int64_t, error> {
    auto const u{read_varint()};
    if (!u)
      return std::unexpected{u.error()};
    auto const decoded{(*u >> 1) ^ (std::uint64_t{0} - (*u & std::uint64_t{1}))};
    return static_cast<std::int64_t>(decoded);
  }

  /**
   * @brief Read a length-prefixed string written by
   *        \c writer::write(std::string_view).
   *
   * Decodes the varint length prefix and returns a view of the following
   * bytes directly into the source buffer (zero-copy). The view stays
   * valid only as long as the backing storage does.
   *
   * @return A view of the decoded string body on success.
   *
   * @pre None.
   * @post On success \c position() advances past the length prefix and
   *       the string body; on failure the cursor is left wherever the
   *       partial decode reached.
   *
   * @throws None. Returns \c error::string_too_long when the length
   *         prefix exceeds the per-instance cap, or
   *         \c error::buffer_underrun when fewer bytes remain than the
   *         prefix claims.
   *
   * @warning The returned view is invalidated when the source buffer is
   *          destroyed or modified.
   */
  [[nodiscard]] auto read_string() noexcept -> std::expected<std::string_view, error> {
    auto const len{read_varint()};
    if (!len)
      return std::unexpected{len.error()};
    if (*len > m_max_str) [[unlikely]]
      return std::unexpected{error::string_too_long};
    auto const n{static_cast<size_type>(*len)};
    if (!has(n)) [[unlikely]]
      return std::unexpected{error::buffer_underrun};
    auto const sv{std::string_view{reinterpret_cast<char const*>(m_buf.data() + m_pos), n}};
    m_pos += n;
    return sv;
  }
};

}  // namespace nexenne::serialization::binary
