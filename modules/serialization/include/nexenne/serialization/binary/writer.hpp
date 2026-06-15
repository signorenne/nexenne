#pragma once

/**
 * @file
 * @brief Schema-driven binary writer over a caller-provided buffer.
 *
 * No type tags, no framing, the layout is whatever the producer
 * and consumer agree on. Both sides walk the same schema in the
 * same order, so payloads are as compact as the data itself.
 *
 * Output endianness is little-endian for all integers and floats,
 * independent of host endianness, encoded payloads round-trip
 * across machines. On the common embedded targets (Cortex-M, ESP32,
 * RP2040, AVR, RISC-V) the host is already little-endian so the
 * write reduces to a single memcpy.
 *
 * String lengths are encoded as unsigned LEB128 varints, so short
 * strings cost a single byte of overhead. Signed integers may be
 * encoded with \c write_zigzag for variable-length encoding of
 * small negative values (1 byte for ±63, 2 bytes for ±8191, etc.)
 *
 * All write operations return \c std::expected<void, error>:
 *
 *   - \c error::buffer_full     when the destination span is out
 *                                of room.
 *   - \c error::string_too_long when the string length doesn't
 *                                fit in 32 bits (safety cap).
 *
 * Heap-free, exception-free, and \c noexcept end to end, drop in
 * directly on bare-metal MCUs, hand the writer a buffer sized
 * exactly for the message, ship it over UART/SPI/CAN/BLE without
 * a copy.
 *
 * Typical use:
 *
 * \code
 * auto buf{std::array<std::byte, 256>{}};
 * auto w{writer{buf}};
 * w.write(std::uint32_t{42});
 * w.write(3.14);
 * w.write("hello");
 * auto const written{w.bytes_written()};
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
 * @brief Schema-driven binary writer over a caller-provided byte span.
 *
 * Encodes integers and floats in little-endian order regardless of host
 * endianness, length-prefixes strings with LEB128 varints, and emits no
 * type tags, producer and consumer agree on the layout out of band. The
 * destination is a \c std::span the caller owns, so the writer never
 * allocates and reports overflow through \c std::expected.
 *
 * The matching decoder is \c reader, which must read fields in the same
 * order they were written.
 *
 * @note All write operations are \c noexcept and never allocate.
 */
class writer {
public:
  using byte_type = std::byte;
  using size_type = std::size_t;

private:
  std::span<byte_type> m_buf{};
  size_type m_pos{0};

  [[nodiscard]] constexpr auto fits(size_type const n) const noexcept -> bool {
    return n <= m_buf.size() - m_pos;
  }

  // True when a length prefix plus a body fit, computed without overflowing
  // size_type (prefix + body could wrap on a 32-bit target with a huge body).
  [[nodiscard]] constexpr auto
  fits_prefixed(size_type const prefix, size_type const body) const noexcept -> bool {
    auto const remaining{m_buf.size() - m_pos};
    return prefix <= remaining && body <= remaining - prefix;
  }

  static constexpr auto varint_size(std::uint64_t value) noexcept -> size_type {
    auto n{size_type{1}};
    while (value >= 0x80) {
      value >>= 7;
      ++n;
    }
    return n;
  }

public:
  /**
   * @brief Construct a writer over the mutable byte span \p buf.
   *
   * The cursor starts at offset zero. The writer stores the span by
   * value but does not own the underlying bytes.
   *
   * @param buf  Destination bytes to fill. Must outlive the writer.
   *
   * @pre \p buf refers to writable memory for the lifetime of the
   *       writer.
   * @post \c bytes_written() is zero and \c bytes_remaining() equals
   *       \c buf.size().
   */
  explicit constexpr writer(std::span<byte_type> const buf) noexcept : m_buf{buf} {}

  /**
   * @brief Number of bytes emitted so far.
   *
   * @return Current cursor offset from the start of the buffer.
   *
   * @pre None.
   * @post Result is in the range \c [0, capacity()].
   */
  [[nodiscard]] constexpr auto bytes_written() const noexcept -> size_type {
    return m_pos;
  }

  /**
   * @brief Free space left in the destination buffer.
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
   * @brief Total size of the destination buffer.
   *
   * @return Number of bytes in the span the writer was constructed with.
   *
   * @pre None.
   * @post Result is constant for the lifetime of the writer.
   */
  [[nodiscard]] constexpr auto capacity() const noexcept -> size_type {
    return m_buf.size();
  }

  /**
   * @brief Pointer to the start of the destination buffer.
   *
   * @return Address of the first byte of the backing span.
   *
   * @pre None.
   * @post The returned pointer is constant for the lifetime of the
   *       writer; only the first \c bytes_written() bytes are
   *       meaningful.
   */
  [[nodiscard]] constexpr auto data() const noexcept -> byte_type const* {
    return m_buf.data();
  }

  /**
   * @brief View of the bytes written so far.
   *
   * Feed the result directly to a transport, for example a span over a
   * TX DMA buffer or a socket write.
   *
   * @return A span covering the populated prefix of the buffer.
   *
   * @pre None.
   * @post The returned span has size \c bytes_written().
   */
  [[nodiscard]] constexpr auto written() const noexcept -> std::span<byte_type const> {
    return std::span<byte_type const>{m_buf.data(), m_pos};
  }

  /**
   * @brief Rewind the cursor to offset zero without clearing the buffer.
   *
   * Reuse the same backing storage for the next message; previously
   * written bytes remain until overwritten.
   *
   * @pre None.
   * @post \c bytes_written() is zero.
   */
  constexpr auto reset() noexcept -> void {
    m_pos = 0;
  }

  /**
   * @brief Advance the cursor by \p n bytes, leaving them untouched.
   *
   * Reserve room for a length prefix or header to backfill later.
   * Bounds-checked.
   *
   * @param n  Number of bytes to skip.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c bytes_written() increases by \p n; on failure
   *       the cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when fewer than \p n
   *         bytes remain.
   */
  auto skip(size_type const n) noexcept -> std::expected<void, error> {
    if (!fits(n)) [[unlikely]]
      return std::unexpected{error::buffer_full};
    m_pos += n;
    return {};
  }

  /**
   * @brief Write one primitive in little-endian byte order.
   *
   * One \c memcpy on the little-endian fast path; on a big-endian host
   * the bytes are reversed first. Works for any built-in integer or
   * floating-point type.
   *
   * @tparam T  Trivially-copyable integral or floating-point type.
   * @param value  Value to encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c bytes_written() increases by \c sizeof(T); on
   *       failure the cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when fewer than
   *         \c sizeof(T) bytes remain.
   */
  template <typename T>
    requires std::is_trivially_copyable_v<T> && (std::integral<T> || std::floating_point<T>)
  auto write(T const value) noexcept -> std::expected<void, error> {
    if (!fits(sizeof(T))) [[unlikely]]
      return std::unexpected{error::buffer_full};
    if constexpr (std::endian::native == std::endian::little) {
      std::memcpy(m_buf.data() + m_pos, &value, sizeof(T));
    } else {
      // Cold path, every supported MCU target is little-endian.
      auto bytes{std::array<byte_type, sizeof(T)>{}};
      std::memcpy(bytes.data(), &value, sizeof(T));
      for (size_type i{0}; i < sizeof(T); ++i) {
        m_buf[m_pos + i] = bytes[sizeof(T) - 1 - i];
      }
    }
    m_pos += sizeof(T);
    return {};
  }

  /**
   * @brief Write a raw byte block with no length prefix.
   *
   * Use when the schema fixes or otherwise communicates the size out of
   * band (fixed arrays, sentinel-terminated blocks, etc).
   *
   * @param data  Bytes to copy verbatim into the buffer.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c bytes_written() increases by \c data.size(); on
   *       failure the cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when fewer than
   *         \c data.size() bytes remain.
   */
  auto write_bytes(std::span<byte_type const> const data) noexcept -> std::expected<void, error> {
    if (!fits(data.size())) [[unlikely]]
      return std::unexpected{error::buffer_full};
    std::memcpy(m_buf.data() + m_pos, data.data(), data.size());
    m_pos += data.size();
    return {};
  }

  /**
   * @brief Bulk-write a span of primitives in little-endian order.
   *
   * One \c memcpy on little-endian hosts (or whenever \c sizeof(T) is 1);
   * otherwise each element is byte-swapped through \c write. A single
   * bounds check fronts the whole batch.
   *
   * @tparam T  Trivially-copyable integral or floating-point type.
   * @param xs  Elements to encode, in order.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c bytes_written() increases by
   *       \c xs.size()*sizeof(T); on failure the cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when fewer than
   *         \c xs.size()*sizeof(T) bytes remain.
   */
  template <typename T>
    requires std::is_trivially_copyable_v<T> && (std::integral<T> || std::floating_point<T>)
  auto write_array(std::span<T const> const xs) noexcept -> std::expected<void, error> {
    // Compare counts, not byte totals: xs.size() * sizeof(T) could overflow
    // size_t and wrap to a small value that passes a bounds check, then memcpy
    // would over-read the source span.
    if (xs.size() > (m_buf.size() - m_pos) / sizeof(T)) [[unlikely]] {
      return std::unexpected{error::buffer_full};
    }
    auto const n{xs.size() * sizeof(T)};
    if constexpr (std::endian::native == std::endian::little || sizeof(T) == 1) {
      std::memcpy(m_buf.data() + m_pos, xs.data(), n);
      m_pos += n;
    } else {
      for (auto const& x : xs) {
        static_cast<void>(write(x));  // sub-write already bounds-checked above
      }
    }
    return {};
  }

  /**
   * @brief Write an unsigned LEB128 varint.
   *
   * Emits 7 bits of magnitude per byte with the high bit signalling that
   * more bytes follow, so small values cost a single byte. A single
   * bounds check fronts the whole encode.
   *
   * @param value  Unsigned value to encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c bytes_written() increases by 1 to 10 bytes; on
   *       failure the cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the encoded varint
   *         does not fit in the remaining space.
   */
  auto write_varint(std::uint64_t value) noexcept -> std::expected<void, error> {
    auto const n{varint_size(value)};
    if (!fits(n)) [[unlikely]]
      return std::unexpected{error::buffer_full};
    while (value >= 0x80) {
      m_buf[m_pos++] = static_cast<byte_type>((value & 0x7F) | 0x80);
      value >>= 7;
    }
    m_buf[m_pos++] = static_cast<byte_type>(value);
    return {};
  }

  /**
   * @brief Write a signed integer using zigzag plus LEB128 encoding.
   *
   * Maps the value through zigzag so small magnitudes of either sign
   * encode in one byte, with cost growing by roughly one byte per 7 bits
   * of magnitude. This mirrors protobuf's \c sint32 / \c sint64 wire
   * format. Decode with \c reader::read_zigzag.
   *
   * @param value  Signed value to encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success \c bytes_written() increases by 1 to 10 bytes; on
   *       failure the cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the encoded value
   *         does not fit in the remaining space.
   */
  auto write_zigzag(std::int64_t const value) noexcept -> std::expected<void, error> {
    // Zig-zag in the unsigned domain: a signed left shift of a negative value
    // is undefined behaviour. (value >> 63 is an arithmetic shift, 0 or all-ones.)
    auto const u{
      (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63)
    };
    return write_varint(u);
  }

  /**
   * @brief Write a string as a varint length prefix followed by its
   *        bytes.
   *
   * The length is encoded as an unsigned LEB128 varint, then the bytes
   * of \p s are copied verbatim. Decode with \c reader::read_string. The
   * length is capped at \c 2^32-1 to keep prefixes sane.
   *
   * @param s  String to encode. Its bytes are copied as-is and are
   *           expected to be valid UTF-8, though the writer does not
   *           validate the encoding.
   *
   * @return Empty on success.
   *
   * @pre \p s contains fewer than \c 2^32 bytes.
   * @post On success \c bytes_written() increases by the prefix size
   *       plus \c s.size(); on failure the cursor is unchanged.
   *
   * @throws None. Returns \c error::string_too_long when \p s exceeds
   *         \c 2^32-1 bytes, or \c error::buffer_full when the prefix
   *         plus body does not fit in the remaining space.
   */
  auto write(std::string_view const s) noexcept -> std::expected<void, error> {
    if (s.size() > 0xFFFFFFFFu) [[unlikely]]
      return std::unexpected{error::string_too_long};
    auto const len_n{varint_size(s.size())};
    if (!fits_prefixed(len_n, s.size())) [[unlikely]]
      return std::unexpected{error::buffer_full};
    auto v{s.size()};
    while (v >= 0x80) {
      m_buf[m_pos++] = static_cast<byte_type>((v & 0x7F) | 0x80);
      v >>= 7;
    }
    m_buf[m_pos++] = static_cast<byte_type>(v);
    std::memcpy(m_buf.data() + m_pos, s.data(), s.size());
    m_pos += s.size();
    return {};
  }
};

}  // namespace nexenne::serialization::binary
