#pragma once

/**
 * @file
 * @brief MessagePack writer / reader (subset, no extension types).
 *
 * MessagePack is a self-describing binary format that compresses
 * a JSON-like document into ~half the size while preserving full
 * type information (the schema lives inside the payload, unlike
 * \c binary::writer which is schema-driven).
 *
 * Wire format covered:
 *
 *   - nil
 *   - bool (true / false)
 *   - integers: positive fixint, negative fixint, uint 8/16/32/64,
 *     int 8/16/32/64, encoder picks the smallest fitting form.
 *   - floats: float32, float64
 *   - str 8/16/32 and fixstr
 *   - bin 8/16/32
 *   - fixarray / array16 / array32 (length only, elements follow)
 *   - fixmap / map16 / map32   (length only, key/value pairs follow)
 *
 * Not covered (skipped on read with an error): ext types, timestamp
 * extensions. They can be added without source breakage.
 *
 * Both reader and writer are heap-free, exception-free, \c noexcept,
 * and operate on caller-provided spans. Output is always big-endian
 * as the MessagePack spec mandates.
 *
 * Typical encode:
 *
 * \code
 * std::array<std::byte, 64> buf{};
 * msgpack::writer w{buf};
 * w.write_map_header(2);
 *   w.write_string("id");
 *   w.write_int(42);
 *   w.write_string("name");
 *   w.write_string("oslo");
 * \endcode
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>

#include <nexenne/serialization/error.hpp>

namespace nexenne::serialization::msgpack {

/**
 * @brief Logical kind of the next MessagePack item, as reported by
 *        \c reader::peek_type.
 *
 * Collapses the format's many one-byte type prefixes into a flat set the
 * caller can branch on before choosing a \c read_* call.
 */
enum class type : std::uint8_t {
  nil,           ///< The nil value.
  boolean,       ///< A boolean.
  integer,       ///< Signed or unsigned integer; read via \c read_int.
  floating,      ///< 32- or 64-bit float; \c read_float returns double.
  string,        ///< A UTF-8 string.
  binary,        ///< A raw byte blob.
  array_header,  ///< Length prefix; followed by N values.
  map_header,    ///< Length prefix; followed by N key-value pairs.
};

namespace detail {

// Store an unsigned value big-endian. On a little-endian host (every MCU target
// and x86) this is a byteswap plus a memcpy, which the compiler lowers to a
// single REV/BSWAP and store; on a big-endian host it is a plain memcpy.
template <std::unsigned_integral U>
inline auto store_be(std::byte* const dst, U value) noexcept -> void {
  if constexpr (std::endian::native == std::endian::little) {
    value = std::byteswap(value);
  }
  std::memcpy(dst, &value, sizeof(value));
}

template <std::unsigned_integral U>
[[nodiscard]] inline auto load_be(std::byte const* const src) noexcept -> U {
  U value{};
  std::memcpy(&value, src, sizeof(value));
  if constexpr (std::endian::native == std::endian::little) {
    value = std::byteswap(value);
  }
  return value;
}

inline auto store_be16(std::byte* const dst, std::uint16_t const v) noexcept -> void {
  store_be(dst, v);
}

inline auto store_be32(std::byte* const dst, std::uint32_t const v) noexcept -> void {
  store_be(dst, v);
}

inline auto store_be64(std::byte* const dst, std::uint64_t const v) noexcept -> void {
  store_be(dst, v);
}

[[nodiscard]] inline auto load_be16(std::byte const* const src) noexcept -> std::uint16_t {
  return load_be<std::uint16_t>(src);
}

[[nodiscard]] inline auto load_be32(std::byte const* const src) noexcept -> std::uint32_t {
  return load_be<std::uint32_t>(src);
}

[[nodiscard]] inline auto load_be64(std::byte const* const src) noexcept -> std::uint64_t {
  return load_be<std::uint64_t>(src);
}

}  // namespace detail

/**
 * @brief MessagePack writer over a caller-provided span.
 *
 * Encodes a self-describing MessagePack stream into a \c std::span the
 * caller owns, with no allocation. Each integer, string, and binary blob
 * is emitted in the smallest fitting wire form, and all multi-byte values
 * are big-endian per the spec. Every emit op is bounds-checked and reports
 * overflow through \c std::expected.
 *
 * @note All operations are \c noexcept and never allocate. Extension and
 *       timestamp types are not supported.
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

  // True when a fixed-size header plus a body fit, computed without overflowing
  // size_type (header + body could wrap on a 32-bit target with a huge body).
  [[nodiscard]] constexpr auto
  fits_prefixed(size_type const header, size_type const body) const noexcept -> bool {
    auto const remaining{m_buf.size() - m_pos};
    return header <= remaining && body <= remaining - header;
  }

  auto put1(std::uint8_t const b) noexcept -> std::expected<void, error> {
    if (!fits(1)) [[unlikely]]
      return std::unexpected{error::buffer_full};
    m_buf[m_pos++] = static_cast<byte_type>(b);
    return {};
  }

public:
  /**
   * @brief Construct a writer over the mutable byte span \p buf.
   *
   * @param buf  Destination bytes to fill. Must outlive the writer.
   *
   * @pre \p buf refers to writable memory for the lifetime of the
   *       writer.
   * @post \c bytes_written() is zero.
   */
  explicit constexpr writer(std::span<byte_type> const buf) noexcept : m_buf{buf} {}

  /**
   * @brief Number of bytes emitted so far.
   *
   * @return Current cursor offset.
   *
   * @pre None.
   * @post Result is in the range \c [0, span size].
   */
  [[nodiscard]] constexpr auto bytes_written() const noexcept -> size_type {
    return m_pos;
  }

  /**
   * @brief Free space left in the destination buffer.
   *
   * @return Bytes remaining before the span is full.
   *
   * @pre None.
   * @post Result plus \c bytes_written() equals the span size.
   */
  [[nodiscard]] constexpr auto bytes_remaining() const noexcept -> size_type {
    return m_buf.size() - m_pos;
  }

  /**
   * @brief View of the bytes written so far.
   *
   * @return A span covering the populated prefix of the buffer.
   *
   * @pre None.
   * @post The returned span has size \c bytes_written().
   */
  [[nodiscard]] constexpr auto written() const noexcept -> std::span<byte_type const> {
    return {m_buf.data(), m_pos};
  }

  /**
   * @brief Rewind the cursor to offset zero without clearing the buffer.
   * @pre None.
   * @post \c bytes_written() is zero.
   */
  constexpr auto reset() noexcept -> void {
    m_pos = 0;
  }

  /**
   * @brief Encode the \c nil value (0xC0).
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_full when no byte remains.
   */
  auto write_nil() noexcept -> std::expected<void, error> {
    return put1(0xC0);
  }

  /**
   * @brief Encode a boolean.
   *
   * @param v  Boolean to write as 0xC3 (true) or 0xC2 (false).
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_full when no byte remains.
   */
  auto write_bool(bool const v) noexcept -> std::expected<void, error> {
    return put1(v ? 0xC3 : 0xC2);
  }

  /**
   * @brief Encode a signed integer in the smallest fitting form.
   *
   * Non-negative values are routed through \c write_uint; negative
   * values use fixint or the int 8/16/32/64 forms.
   *
   * @param v  Signed value to encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by the encoded size; on failure
   *       it is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the encoding does
   *         not fit.
   */
  auto write_int(std::int64_t const v) noexcept -> std::expected<void, error> {
    if (v >= 0)
      return write_uint(static_cast<std::uint64_t>(v));
    if (v >= -32) {
      return put1(static_cast<std::uint8_t>(v));  // negative fixint 0xE0..0xFF
    }
    if (v >= -128) {
      if (!fits(2))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xD0);
      m_buf[m_pos++] = static_cast<byte_type>(static_cast<std::int8_t>(v));
      return {};
    }
    if (v >= -32768) {
      if (!fits(3))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xD1);
      detail::store_be16(
        m_buf.data() + m_pos, static_cast<std::uint16_t>(static_cast<std::int16_t>(v))
      );
      m_pos += 2;
      return {};
    }
    if (v >= -2147483648LL) {
      if (!fits(5))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xD2);
      detail::store_be32(
        m_buf.data() + m_pos, static_cast<std::uint32_t>(static_cast<std::int32_t>(v))
      );
      m_pos += 4;
      return {};
    }
    if (!fits(9))
      return std::unexpected{error::buffer_full};
    m_buf[m_pos++] = static_cast<byte_type>(0xD3);
    detail::store_be64(m_buf.data() + m_pos, static_cast<std::uint64_t>(v));
    m_pos += 8;
    return {};
  }

  /**
   * @brief Encode an unsigned integer in the smallest fitting form.
   *
   * Uses positive fixint or the uint 8/16/32/64 forms.
   *
   * @param v  Unsigned value to encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by the encoded size; on failure
   *       it is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the encoding does
   *         not fit.
   */
  auto write_uint(std::uint64_t const v) noexcept -> std::expected<void, error> {
    if (v <= 0x7F)
      return put1(static_cast<std::uint8_t>(v));  // positive fixint
    if (v <= 0xFF) {
      if (!fits(2))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xCC);
      m_buf[m_pos++] = static_cast<byte_type>(v);
      return {};
    }
    if (v <= 0xFFFF) {
      if (!fits(3))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xCD);
      detail::store_be16(m_buf.data() + m_pos, static_cast<std::uint16_t>(v));
      m_pos += 2;
      return {};
    }
    if (v <= 0xFFFFFFFFu) {
      if (!fits(5))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xCE);
      detail::store_be32(m_buf.data() + m_pos, static_cast<std::uint32_t>(v));
      m_pos += 4;
      return {};
    }
    if (!fits(9))
      return std::unexpected{error::buffer_full};
    m_buf[m_pos++] = static_cast<byte_type>(0xCF);
    detail::store_be64(m_buf.data() + m_pos, v);
    m_pos += 8;
    return {};
  }

  /**
   * @brief Encode a single-precision float (0xCA prefix).
   *
   * @param v  Value to encode as IEEE-754 float32 in big-endian order.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by five bytes; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_full when five bytes do not
   *         remain.
   */
  auto write_float32(float const v) noexcept -> std::expected<void, error> {
    if (!fits(5))
      return std::unexpected{error::buffer_full};
    m_buf[m_pos++] = static_cast<byte_type>(0xCA);
    detail::store_be32(m_buf.data() + m_pos, std::bit_cast<std::uint32_t>(v));
    m_pos += 4;
    return {};
  }

  /**
   * @brief Encode a double-precision float (0xCB prefix).
   *
   * @param v  Value to encode as IEEE-754 float64 in big-endian order.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by nine bytes; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_full when nine bytes do not
   *         remain.
   */
  auto write_float64(double const v) noexcept -> std::expected<void, error> {
    if (!fits(9))
      return std::unexpected{error::buffer_full};
    m_buf[m_pos++] = static_cast<byte_type>(0xCB);
    detail::store_be64(m_buf.data() + m_pos, std::bit_cast<std::uint64_t>(v));
    m_pos += 8;
    return {};
  }

  /**
   * @brief Encode a UTF-8 string in the smallest fitting form.
   *
   * Uses fixstr or the str 8/16/32 forms, then copies the bytes.
   *
   * @param s  Text to encode; expected to be valid UTF-8, which the
   *           writer does not verify.
   *
   * @return Empty on success.
   *
   * @pre \p s contains fewer than \c 2^32 bytes.
   * @post On success the cursor advances past the header and body; on
   *       failure it is unchanged.
   *
   * @throws None. Returns \c error::string_too_long when \p s exceeds
   *         \c 2^32-1 bytes, or \c error::buffer_full when the header
   *         plus body does not fit.
   */
  auto write_string(std::string_view const s) noexcept -> std::expected<void, error> {
    auto const n{s.size()};
    if (n > 0xFFFFFFFFu)
      return std::unexpected{error::string_too_long};
    if (n <= 31) {
      if (!fits_prefixed(1, n))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xA0 | n);
    } else if (n <= 0xFF) {
      if (!fits_prefixed(2, n))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xD9);
      m_buf[m_pos++] = static_cast<byte_type>(n);
    } else if (n <= 0xFFFF) {
      if (!fits_prefixed(3, n))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xDA);
      detail::store_be16(m_buf.data() + m_pos, static_cast<std::uint16_t>(n));
      m_pos += 2;
    } else {
      if (!fits_prefixed(5, n))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xDB);
      detail::store_be32(m_buf.data() + m_pos, static_cast<std::uint32_t>(n));
      m_pos += 4;
    }
    // memcpy with a null pointer is UB even for size 0.
    if (n != 0) {
      std::memcpy(m_buf.data() + m_pos, s.data(), n);
    }
    m_pos += n;
    return {};
  }

  /**
   * @brief Encode a raw byte blob in the smallest fitting form.
   *
   * Uses the bin 8/16/32 forms, then copies the bytes.
   *
   * @param data  Bytes to encode.
   *
   * @return Empty on success.
   *
   * @pre \p data contains fewer than \c 2^32 bytes.
   * @post On success the cursor advances past the header and body; on
   *       failure it is unchanged.
   *
   * @throws None. Returns \c error::string_too_long when \p data exceeds
   *         \c 2^32-1 bytes, or \c error::buffer_full when the header
   *         plus body does not fit.
   */
  auto write_binary(std::span<byte_type const> const data) noexcept -> std::expected<void, error> {
    auto const n{data.size()};
    if (n > 0xFFFFFFFFu)
      return std::unexpected{error::string_too_long};
    if (n <= 0xFF) {
      if (!fits_prefixed(2, n))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xC4);
      m_buf[m_pos++] = static_cast<byte_type>(n);
    } else if (n <= 0xFFFF) {
      if (!fits_prefixed(3, n))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xC5);
      detail::store_be16(m_buf.data() + m_pos, static_cast<std::uint16_t>(n));
      m_pos += 2;
    } else {
      if (!fits_prefixed(5, n))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xC6);
      detail::store_be32(m_buf.data() + m_pos, static_cast<std::uint32_t>(n));
      m_pos += 4;
    }
    // memcpy with a null pointer is UB even for size 0; an empty payload's
    // data() may be null.
    if (n != 0) {
      std::memcpy(m_buf.data() + m_pos, data.data(), n);
    }
    m_pos += n;
    return {};
  }

  /**
   * @brief Encode an array header of \p n elements.
   *
   * Uses fixarray or the array16/array32 forms. The caller then writes
   * exactly \p n values.
   *
   * @param n  Number of array elements that will follow.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by the header size; on failure
   *       it is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the header does not
   *         fit.
   */
  auto write_array_header(std::uint32_t const n) noexcept -> std::expected<void, error> {
    if (n <= 15)
      return put1(static_cast<std::uint8_t>(0x90 | n));
    if (n <= 0xFFFF) {
      if (!fits(3))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xDC);
      detail::store_be16(m_buf.data() + m_pos, static_cast<std::uint16_t>(n));
      m_pos += 2;
      return {};
    }
    if (!fits(5))
      return std::unexpected{error::buffer_full};
    m_buf[m_pos++] = static_cast<byte_type>(0xDD);
    detail::store_be32(m_buf.data() + m_pos, n);
    m_pos += 4;
    return {};
  }

  /**
   * @brief Encode a map header of \p n key-value pairs.
   *
   * Uses fixmap or the map16/map32 forms. The caller then writes exactly
   * \p n key/value pairs.
   *
   * @param n  Number of key-value pairs that will follow.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by the header size; on failure
   *       it is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the header does not
   *         fit.
   */
  auto write_map_header(std::uint32_t const n) noexcept -> std::expected<void, error> {
    if (n <= 15)
      return put1(static_cast<std::uint8_t>(0x80 | n));
    if (n <= 0xFFFF) {
      if (!fits(3))
        return std::unexpected{error::buffer_full};
      m_buf[m_pos++] = static_cast<byte_type>(0xDE);
      detail::store_be16(m_buf.data() + m_pos, static_cast<std::uint16_t>(n));
      m_pos += 2;
      return {};
    }
    if (!fits(5))
      return std::unexpected{error::buffer_full};
    m_buf[m_pos++] = static_cast<byte_type>(0xDF);
    detail::store_be32(m_buf.data() + m_pos, n);
    m_pos += 4;
    return {};
  }
};

/**
 * @brief Token-stream MessagePack reader.
 *
 * Decoding strategy: call \c peek_type to find out what comes next,
 * then call the matching \c read_* to consume it. Containers
 * (array, map) only return their length, the caller recursively
 * reads the elements.
 *
 * The reader returns string and binary payloads as
 * \c std::string_view / \c std::span over the source buffer, no
 * copy. Lifetime of the returned views is tied to the input span.
 */
class reader {
public:
  using byte_type = std::byte;
  using size_type = std::size_t;

private:
  std::span<byte_type const> m_buf{};
  size_type m_pos{0};

  [[nodiscard]] constexpr auto has(size_type const n) const noexcept -> bool {
    return n <= m_buf.size() - m_pos;
  }

  [[nodiscard]] auto take(size_type const n
  ) noexcept -> std::expected<std::span<byte_type const>, error> {
    if (!has(n)) [[unlikely]]
      return std::unexpected{error::buffer_underrun};
    auto const out{std::span{m_buf.data() + m_pos, n}};
    m_pos += n;
    return out;
  }

public:
  /**
   * @brief Construct a reader over the immutable byte span \p buf.
   *
   * @param buf  Source bytes to decode. Must outlive the reader and any
   *             view it returns.
   *
   * @pre \p buf refers to valid memory for the lifetime of the reader.
   * @post \c bytes_read() is zero.
   */
  explicit constexpr reader(std::span<byte_type const> const buf) noexcept : m_buf{buf} {}

  /**
   * @brief Number of bytes consumed so far.
   *
   * @return Current cursor offset.
   *
   * @pre None.
   * @post Result is in the range \c [0, span size].
   */
  [[nodiscard]] constexpr auto bytes_read() const noexcept -> size_type {
    return m_pos;
  }

  /**
   * @brief Number of bytes left to read.
   *
   * @return Bytes remaining before the end of the buffer.
   *
   * @pre None.
   * @post Result plus \c bytes_read() equals the span size.
   */
  [[nodiscard]] constexpr auto bytes_remaining() const noexcept -> size_type {
    return m_buf.size() - m_pos;
  }

  /**
   * @brief Whether the cursor has reached the end of the buffer.
   *
   * @return \c true when no bytes remain.
   *
   * @pre None.
   * @post Result equals \c (bytes_remaining() == 0).
   */
  [[nodiscard]] constexpr auto at_end() const noexcept -> bool {
    return m_pos == m_buf.size();
  }

  /**
   * @brief Classify the next item without consuming it.
   *
   * Inspects the leading prefix byte to report which \c read_* call
   * applies.
   *
   * @return The kind of the next item on success.
   *
   * @pre None.
   * @post The cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun at end of input, or
   *         \c error::invalid_input for an unsupported prefix (ext or
   *         reserved bytes).
   */
  [[nodiscard]] auto peek_type() const noexcept -> std::expected<type, error> {
    if (!has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_buf[m_pos])};
    if (b == 0xC0)
      return type::nil;
    if (b == 0xC2 || b == 0xC3)
      return type::boolean;
    if (b <= 0x7F || b >= 0xE0)
      return type::integer;  // fixint
    if ((b & 0xE0) == 0xA0)
      return type::string;  // fixstr
    if ((b & 0xF0) == 0x90)
      return type::array_header;  // fixarray
    if ((b & 0xF0) == 0x80)
      return type::map_header;  // fixmap
    switch (b) {
      case 0xCA:
      case 0xCB:
        return type::floating;
      case 0xCC:
      case 0xCD:
      case 0xCE:
      case 0xCF:
      case 0xD0:
      case 0xD1:
      case 0xD2:
      case 0xD3:
        return type::integer;
      case 0xD9:
      case 0xDA:
      case 0xDB:
        return type::string;
      case 0xC4:
      case 0xC5:
      case 0xC6:
        return type::binary;
      case 0xDC:
      case 0xDD:
        return type::array_header;
      case 0xDE:
      case 0xDF:
        return type::map_header;
      default:
        return std::unexpected{error::invalid_input};
    }
  }

  /**
   * @brief Consume a \c nil value (0xC0).
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::type_mismatch when the next byte is
   *         not 0xC0 (also returned at end of input).
   */
  auto read_nil() noexcept -> std::expected<void, error> {
    if (!has(1) || static_cast<std::uint8_t>(m_buf[m_pos]) != 0xC0) {
      return std::unexpected{error::type_mismatch};
    }
    ++m_pos;
    return {};
  }

  /**
   * @brief Read a boolean.
   *
   * @return \c false for 0xC2 and \c true for 0xC3.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun at end of input, or
   *         \c error::type_mismatch when the next byte is not a boolean.
   */
  [[nodiscard]] auto read_bool() noexcept -> std::expected<bool, error> {
    if (!has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_buf[m_pos])};
    if (b == 0xC2) {
      ++m_pos;
      return false;
    }
    if (b == 0xC3) {
      ++m_pos;
      return true;
    }
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Read an integer of any MessagePack integer form.
   *
   * Handles positive and negative fixint and the int/uint 8/16/32/64
   * forms, widening the result to \c int64_t.
   *
   * @return The decoded value on success.
   *
   * @pre None.
   * @post On success the cursor advances past the item; on failure it is
   *       unchanged or rewound to before the prefix byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not an integer.
   *
   * @warning A uint64 above \c INT64_MAX wraps to a negative
   *          \c int64_t; this reader does not widen to an unsigned
   *          result.
   */
  [[nodiscard]] auto read_int() noexcept -> std::expected<std::int64_t, error> {
    if (!has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_buf[m_pos])};
    if (b <= 0x7F) {
      ++m_pos;
      return static_cast<std::int64_t>(b);
    }
    if (b >= 0xE0) {
      ++m_pos;
      return static_cast<std::int64_t>(static_cast<std::int8_t>(b));
    }
    ++m_pos;
    switch (b) {
      case 0xCC: {
        auto p{take(1)};
        if (!p)
          return std::unexpected{p.error()};
        return static_cast<std::int64_t>(static_cast<std::uint8_t>((*p)[0]));
      }
      case 0xCD: {
        auto p{take(2)};
        if (!p)
          return std::unexpected{p.error()};
        return static_cast<std::int64_t>(detail::load_be16(p->data()));
      }
      case 0xCE: {
        auto p{take(4)};
        if (!p)
          return std::unexpected{p.error()};
        return static_cast<std::int64_t>(detail::load_be32(p->data()));
      }
      case 0xCF: {
        auto p{take(8)};
        if (!p)
          return std::unexpected{p.error()};
        return static_cast<std::int64_t>(detail::load_be64(p->data()));
      }
      case 0xD0: {
        auto p{take(1)};
        if (!p)
          return std::unexpected{p.error()};
        return static_cast<std::int64_t>(static_cast<std::int8_t>(static_cast<std::uint8_t>((*p)[0])
        ));
      }
      case 0xD1: {
        auto p{take(2)};
        if (!p)
          return std::unexpected{p.error()};
        return static_cast<std::int64_t>(static_cast<std::int16_t>(detail::load_be16(p->data())));
      }
      case 0xD2: {
        auto p{take(4)};
        if (!p)
          return std::unexpected{p.error()};
        return static_cast<std::int64_t>(static_cast<std::int32_t>(detail::load_be32(p->data())));
      }
      case 0xD3: {
        auto p{take(8)};
        if (!p)
          return std::unexpected{p.error()};
        return static_cast<std::int64_t>(detail::load_be64(p->data()));
      }
      default:
        --m_pos;
        return std::unexpected{error::type_mismatch};
    }
  }

  /**
   * @brief Read a floating-point value as a \c double.
   *
   * Accepts float32 (0xCA) and float64 (0xCB); the single-precision form
   * is widened to \c double.
   *
   * @return The decoded value on success.
   *
   * @pre None.
   * @post On success the cursor advances past the item; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not a float.
   */
  [[nodiscard]] auto read_float() noexcept -> std::expected<double, error> {
    if (!has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_buf[m_pos])};
    if (b == 0xCA) {
      ++m_pos;
      auto p{take(4)};
      if (!p)
        return std::unexpected{p.error()};
      return static_cast<double>(std::bit_cast<float>(detail::load_be32(p->data())));
    }
    if (b == 0xCB) {
      ++m_pos;
      auto p{take(8)};
      if (!p)
        return std::unexpected{p.error()};
      return std::bit_cast<double>(detail::load_be64(p->data()));
    }
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Read a string (fixstr or str 8/16/32) as a view.
   *
   * Zero-copy: the returned view aliases the source buffer.
   *
   * @return A view of the string body on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header and body; on
   *       failure it is unchanged or rewound to before the prefix byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not a string.
   *
   * @warning The returned view is invalidated when the source buffer is
   *          destroyed or modified.
   */
  [[nodiscard]] auto read_string() noexcept -> std::expected<std::string_view, error> {
    if (!has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_buf[m_pos++])};
    std::size_t n{0};
    if ((b & 0xE0) == 0xA0) {
      n = b & 0x1F;
    } else if (b == 0xD9) {
      auto p{take(1)};
      if (!p)
        return std::unexpected{p.error()};
      n = static_cast<std::uint8_t>((*p)[0]);
    } else if (b == 0xDA) {
      auto p{take(2)};
      if (!p)
        return std::unexpected{p.error()};
      n = detail::load_be16(p->data());
    } else if (b == 0xDB) {
      auto p{take(4)};
      if (!p)
        return std::unexpected{p.error()};
      n = detail::load_be32(p->data());
    } else {
      --m_pos;
      return std::unexpected{error::type_mismatch};
    }
    auto p{take(n)};
    if (!p)
      return std::unexpected{p.error()};
    return std::string_view{reinterpret_cast<char const*>(p->data()), n};
  }

  /**
   * @brief Read a binary blob (bin 8/16/32) as a view.
   *
   * Zero-copy: the returned span aliases the source buffer.
   *
   * @return A span over the blob body on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header and body; on
   *       failure it is unchanged or rewound to before the prefix byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not a binary
   *         blob.
   *
   * @warning The returned span is invalidated when the source buffer is
   *          destroyed or modified.
   */
  [[nodiscard]] auto read_binary() noexcept -> std::expected<std::span<byte_type const>, error> {
    if (!has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_buf[m_pos++])};
    std::size_t n{0};
    if (b == 0xC4) {
      auto p{take(1)};
      if (!p)
        return std::unexpected{p.error()};
      n = static_cast<std::uint8_t>((*p)[0]);
    } else if (b == 0xC5) {
      auto p{take(2)};
      if (!p)
        return std::unexpected{p.error()};
      n = detail::load_be16(p->data());
    } else if (b == 0xC6) {
      auto p{take(4)};
      if (!p)
        return std::unexpected{p.error()};
      n = detail::load_be32(p->data());
    } else {
      --m_pos;
      return std::unexpected{error::type_mismatch};
    }
    return take(n);
  }

  /**
   * @brief Read an array header (fixarray or array16/array32).
   *
   * The caller then reads exactly that many elements.
   *
   * @return The element count on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header; on failure it
   *       is unchanged or rewound to before the prefix byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not an array
   *         header.
   */
  [[nodiscard]] auto read_array_header() noexcept -> std::expected<std::uint32_t, error> {
    if (!has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_buf[m_pos++])};
    if ((b & 0xF0) == 0x90)
      return static_cast<std::uint32_t>(b & 0x0F);
    if (b == 0xDC) {
      auto p{take(2)};
      if (!p)
        return std::unexpected{p.error()};
      return detail::load_be16(p->data());
    }
    if (b == 0xDD) {
      auto p{take(4)};
      if (!p)
        return std::unexpected{p.error()};
      return detail::load_be32(p->data());
    }
    --m_pos;
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Read a map header (fixmap or map16/map32).
   *
   * The caller then reads exactly that many key/value pairs.
   *
   * @return The number of key-value pairs on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header; on failure it
   *       is unchanged or rewound to before the prefix byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not a map
   *         header.
   */
  [[nodiscard]] auto read_map_header() noexcept -> std::expected<std::uint32_t, error> {
    if (!has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_buf[m_pos++])};
    if ((b & 0xF0) == 0x80)
      return static_cast<std::uint32_t>(b & 0x0F);
    if (b == 0xDE) {
      auto p{take(2)};
      if (!p)
        return std::unexpected{p.error()};
      return detail::load_be16(p->data());
    }
    if (b == 0xDF) {
      auto p{take(4)};
      if (!p)
        return std::unexpected{p.error()};
      return detail::load_be32(p->data());
    }
    --m_pos;
    return std::unexpected{error::type_mismatch};
  }
};

}  // namespace nexenne::serialization::msgpack
