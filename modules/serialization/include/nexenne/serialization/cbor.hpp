#pragma once

/**
 * @file
 * @brief CBOR (RFC 8949) writer / reader, subset covering the
 *        eight major types except tagged values and indefinite
 *        lengths.
 *
 * CBOR is the IETF binary equivalent of JSON used by COSE, COAP,
 * WebAuthn / FIDO2, DNS-over-HTTPS responses, and most low-power
 * IoT stacks. Compared to MessagePack it shares the same general
 * shape (one-byte type tag with optional length) but uses a
 * cleaner 3-bit major / 5-bit additional info split.
 *
 * Coverage:
 *
 *   - Major 0 / 1, unsigned and negative integers
 *   - Major 2     - byte string (definite length)
 *   - Major 3     - UTF-8 text string (definite length)
 *   - Major 4     - array (definite length)
 *   - Major 5     - map (definite length)
 *   - Major 7     - simple values (false / true / null / undefined)
 *                   and IEEE-754 float32 / float64. Float16 read is
 *                   supported (converted to double); writer always
 *                   emits float32 or float64.
 *
 * Not covered: major 6 tags, indefinite-length encodings, big
 * integers wrapped in tags. They can be added without breaking
 * the existing API.
 *
 * Output is canonical (deterministic): integers are encoded with
 * the smallest representation, and the writer never emits
 * indefinite-length items.
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <span>
#include <string_view>

#include <nexenne/serialization/error.hpp>
#include <nexenne/utility/buffer_cursor.hpp>
#include <nexenne/utility/endian.hpp>

namespace nexenne::serialization::cbor {

/**
 * @brief Logical kind of the next CBOR item, as reported by
 *        \c reader::peek_type.
 *
 * Collapses the CBOR major type and the simple-value sub-tags into a flat
 * set the caller can branch on before choosing a \c read_* call. Tags
 * (major 6) are not represented because they are unsupported.
 */
enum class type : std::uint8_t {
  unsigned_int,  ///< Major 0: non-negative integer.
  negative_int,  ///< Major 1: negative integer.
  byte_string,   ///< Major 2: definite-length byte string.
  text_string,   ///< Major 3: definite-length UTF-8 text.
  array_header,  ///< Major 4: array length follows.
  map_header,    ///< Major 5: map pair-count follows.
  boolean,       ///< Major 7 simple value 20 or 21.
  null,          ///< Major 7 simple value 22.
  undefined,     ///< Major 7 simple value 23.
  floating,      ///< Major 7 half, single, or double float.
};

namespace detail {

// Store an unsigned value big-endian. The byte order is handled by
// nexenne::utility, which writes the bytes most-significant-first into the
// fixed-extent destination span.
template <std::unsigned_integral U>
inline auto store_be(std::byte* const dst, U value) noexcept -> void {
  nexenne::utility::write_be(std::span<std::byte, sizeof(U)>{dst, sizeof(U)}, value);
}

template <std::unsigned_integral U>
[[nodiscard]] inline auto load_be(std::byte const* const src) noexcept -> U {
  return nexenne::utility::read_be<U>(std::span<std::byte const, sizeof(U)>{src, sizeof(U)});
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

// Convert an IEEE 754 half-precision bit pattern to double (reads only).
inline auto half_to_double(std::uint16_t const h) noexcept -> double {
  auto const exp{static_cast<std::uint32_t>((h >> 10) & 0x1F)};
  auto const mant{static_cast<std::uint32_t>(h & 0x3FF)};
  auto const sign{static_cast<std::uint32_t>((h >> 15) & 1)};
  std::uint32_t f{sign << 31};
  if (exp == 0) {
    if (mant == 0) {
      // signed zero
    } else {
      // subnormal, renormalise into single-precision. The counter starts at -1
      // so the synthesized exponent (127 - 15 - e) lands on the correct power of
      // two; starting at 1 decoded every subnormal to a quarter of its value.
      auto m{mant};
      int e{-1};
      while ((m & 0x400) == 0) {
        m <<= 1;
        ++e;
      }
      m &= 0x3FF;
      f |= (static_cast<std::uint32_t>(127 - 15 - e) << 23) | (m << 13);
    }
  } else if (exp == 31) {
    f |= (255u << 23) | (mant << 13);  // inf / NaN
  } else {
    f |= ((exp + 127 - 15) << 23) | (mant << 13);
  }
  return static_cast<double>(std::bit_cast<float>(f));
}

/**
 * @brief Narrow a CBOR 64-bit length argument to a platform size type.
 *
 * A CBOR length prefix is up to 64 bits wide. On a 32-bit target the platform
 * \c size_type cannot hold a value above 4 GiB, so a blind \c static_cast would
 * truncate it and pass a wrong-length view through the bounds check. This guard
 * rejects any length the size type cannot represent. It is templated on the size
 * type so the 32-bit narrowing path is unit-testable on a 64-bit host.
 *
 * @tparam SizeT Target size type (the reader passes its \c size_type).
 * @param len The decoded 64-bit length argument.
 *
 * @return The length as \c SizeT, or \c error::string_too_long when it exceeds
 *         the range of \c SizeT.
 */
template <std::unsigned_integral SizeT>
[[nodiscard]] constexpr auto length_to_size(std::uint64_t const len
) noexcept -> std::expected<SizeT, error> {
  if (len > static_cast<std::uint64_t>(std::numeric_limits<SizeT>::max())) {
    return std::unexpected{error::string_too_long};
  }
  return static_cast<SizeT>(len);
}

}  // namespace detail

/**
 * @brief Canonical CBOR (RFC 8949) writer over a caller-provided span.
 *
 * Encodes the CBOR major types this module supports into a \c std::span
 * the caller owns, with no allocation. Integers use the smallest
 * representation and the writer never emits indefinite-length items, so
 * output is deterministic. Every emit op is bounds-checked and reports
 * overflow through \c std::expected.
 *
 * @note All operations are \c noexcept and never allocate.
 */
class writer {
public:
  using byte_type = std::byte;
  using size_type = std::size_t;

private:
  nexenne::utility::buffer_cursor<byte_type> m_cursor;

  // Write a major-type byte followed by the length argument in CBOR's compact
  // 0/1/2/4/8-byte encoding.
  auto write_head(std::uint8_t const major, std::uint64_t const v) noexcept
    -> std::expected<void, error> {
    auto const m{static_cast<std::uint8_t>(major << 5)};
    if (v <= 23) {
      if (!m_cursor.has(1))
        return std::unexpected{error::buffer_full};
      m_cursor.put(static_cast<byte_type>(m | static_cast<std::uint8_t>(v)));
      return {};
    }
    if (v <= 0xFF) {
      if (!m_cursor.has(2))
        return std::unexpected{error::buffer_full};
      m_cursor.put(static_cast<byte_type>(m | 24));
      m_cursor.put(static_cast<byte_type>(v));
      return {};
    }
    if (v <= 0xFFFF) {
      if (!m_cursor.has(3))
        return std::unexpected{error::buffer_full};
      m_cursor.put(static_cast<byte_type>(m | 25));
      detail::store_be16(m_cursor.data(), static_cast<std::uint16_t>(v));
      m_cursor.advance(2);
      return {};
    }
    if (v <= 0xFFFFFFFFu) {
      if (!m_cursor.has(5))
        return std::unexpected{error::buffer_full};
      m_cursor.put(static_cast<byte_type>(m | 26));
      detail::store_be32(m_cursor.data(), static_cast<std::uint32_t>(v));
      m_cursor.advance(4);
      return {};
    }
    if (!m_cursor.has(9))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(m | 27));
    detail::store_be64(m_cursor.data(), v);
    m_cursor.advance(8);
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
  explicit constexpr writer(std::span<byte_type> const buf) noexcept : m_cursor{buf} {}

  /**
   * @brief Number of bytes emitted so far.
   *
   * @return Current cursor offset.
   *
   * @pre None.
   * @post Result is in the range \c [0, capacity of the span].
   */
  [[nodiscard]] constexpr auto bytes_written() const noexcept -> size_type {
    return m_cursor.position();
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
    return m_cursor.remaining();
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
    return m_cursor.consumed();
  }

  /**
   * @brief Rewind the cursor to offset zero without clearing the buffer.
   * @pre None.
   * @post \c bytes_written() is zero.
   */
  constexpr auto reset() noexcept -> void {
    m_cursor.rewind();
  }

  /**
   * @brief Encode an unsigned integer (major type 0).
   *
   * Uses the smallest of the 1/2/3/5/9-byte forms.
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
    return write_head(0, v);
  }

  /**
   * @brief Encode a signed integer, choosing major type 0 or 1.
   *
   * Non-negative values use major type 0; negative values use major
   * type 1, which encodes \c -1-n as argument \c n. The full \c int64_t
   * range is representable.
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
      return write_head(0, static_cast<std::uint64_t>(v));
    return write_head(1, static_cast<std::uint64_t>(-(v + 1)));
  }

  /**
   * @brief Encode a byte string (major type 2).
   *
   * Writes the length header followed by the raw bytes.
   *
   * @param data  Bytes to encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header and body; on
   *       failure it may have advanced past a partially written header.
   *
   * @throws None. Returns \c error::buffer_full when the header or body
   *         does not fit.
   */
  auto write_bytes(std::span<byte_type const> const data) noexcept -> std::expected<void, error> {
    if (auto const r{write_head(2, data.size())}; !r)
      return r;
    if (!m_cursor.has(data.size()))
      return std::unexpected{error::buffer_full};
    // memcpy with a null pointer is UB even for size 0; an empty span's data()
    // may be null.
    if (data.size() != 0) {
      std::memcpy(m_cursor.data(), data.data(), data.size());
    }
    m_cursor.advance(data.size());
    return {};
  }

  /**
   * @brief Encode a UTF-8 text string (major type 3).
   *
   * Writes the length header followed by the string bytes.
   *
   * @param s  Text to encode; expected to be valid UTF-8, which the
   *           writer does not verify.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header and body; on
   *       failure it may have advanced past a partially written header.
   *
   * @throws None. Returns \c error::buffer_full when the header or body
   *         does not fit.
   */
  auto write_string(std::string_view const s) noexcept -> std::expected<void, error> {
    if (auto const r{write_head(3, s.size())}; !r)
      return r;
    if (!m_cursor.has(s.size()))
      return std::unexpected{error::buffer_full};
    // memcpy with a null pointer is UB even for size 0.
    if (!s.empty()) {
      std::memcpy(m_cursor.data(), s.data(), s.size());
    }
    m_cursor.advance(s.size());
    return {};
  }

  /**
   * @brief Encode an array header of \p n elements (major type 4).
   *
   * The caller must then write exactly \p n items.
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
  auto write_array_header(std::uint64_t const n) noexcept -> std::expected<void, error> {
    return write_head(4, n);
  }

  /**
   * @brief Encode a map header of \p n key-value pairs (major type 5).
   *
   * The caller must then write exactly \p n key/value item pairs.
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
  auto write_map_header(std::uint64_t const n) noexcept -> std::expected<void, error> {
    return write_head(5, n);
  }

  /**
   * @brief Encode a boolean simple value.
   *
   * @param v  Boolean to write as CBOR \c true (0xF5) or \c false
   *           (0xF4).
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
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(v ? 0xF5 : 0xF4));
    return {};
  }

  /**
   * @brief Encode the \c null simple value (0xF6).
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_full when no byte remains.
   */
  auto write_null() noexcept -> std::expected<void, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(0xF6));
    return {};
  }

  /**
   * @brief Encode the \c undefined simple value (0xF7).
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_full when no byte remains.
   */
  auto write_undefined() noexcept -> std::expected<void, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(0xF7));
    return {};
  }

  /**
   * @brief Encode a single-precision float (0xFA prefix).
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
    if (!m_cursor.has(5))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(0xFA));
    detail::store_be32(m_cursor.data(), std::bit_cast<std::uint32_t>(v));
    m_cursor.advance(4);
    return {};
  }

  /**
   * @brief Encode a double-precision float (0xFB prefix).
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
    if (!m_cursor.has(9))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(0xFB));
    detail::store_be64(m_cursor.data(), std::bit_cast<std::uint64_t>(v));
    m_cursor.advance(8);
    return {};
  }
};

/**
 * @brief Token-stream CBOR reader.
 *
 * Pattern: \c peek_type then call the matching \c read_*. Array
 * and map headers return the count; the caller reads the elements
 * recursively. Strings and byte strings return zero-copy views
 * into the source buffer.
 */
class reader {
public:
  using byte_type = std::byte;
  using size_type = std::size_t;

private:
  nexenne::utility::buffer_cursor<byte_type const> m_cursor;

  [[nodiscard]] auto read_argument(std::uint8_t const ai
  ) noexcept -> std::expected<std::uint64_t, error> {
    if (ai < 24)
      return ai;
    switch (ai) {
      case 24:
        if (!m_cursor.has(1))
          return std::unexpected{error::buffer_underrun};
        return static_cast<std::uint64_t>(static_cast<std::uint8_t>(m_cursor.next()));
      case 25:
        if (!m_cursor.has(2))
          return std::unexpected{error::buffer_underrun};
        {
          auto v{detail::load_be16(m_cursor.data())};
          m_cursor.advance(2);
          return static_cast<std::uint64_t>(v);
        }
      case 26:
        if (!m_cursor.has(4))
          return std::unexpected{error::buffer_underrun};
        {
          auto v{detail::load_be32(m_cursor.data())};
          m_cursor.advance(4);
          return static_cast<std::uint64_t>(v);
        }
      case 27:
        if (!m_cursor.has(8))
          return std::unexpected{error::buffer_underrun};
        {
          auto v{detail::load_be64(m_cursor.data())};
          m_cursor.advance(8);
          return v;
        }
      default:
        return std::unexpected{error::invalid_input};
    }
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
  explicit constexpr reader(std::span<byte_type const> const buf) noexcept : m_cursor{buf} {}

  /**
   * @brief Number of bytes consumed so far.
   *
   * @return Current cursor offset.
   *
   * @pre None.
   * @post Result is in the range \c [0, span size].
   */
  [[nodiscard]] constexpr auto bytes_read() const noexcept -> size_type {
    return m_cursor.position();
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
    return m_cursor.remaining();
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
    return m_cursor.exhausted();
  }

  /**
   * @brief Classify the next item without consuming it.
   *
   * Inspects the leading byte to report which \c read_* call applies.
   *
   * @return The kind of the next item on success.
   *
   * @pre None.
   * @post The cursor is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun at end of input, or
   *         \c error::invalid_input for an unsupported major type (a tag)
   *         or an unrecognised simple value.
   */
  [[nodiscard]] auto peek_type() const noexcept -> std::expected<type, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    switch (b >> 5) {
      case 0:
        return type::unsigned_int;
      case 1:
        return type::negative_int;
      case 2:
        return type::byte_string;
      case 3:
        return type::text_string;
      case 4:
        return type::array_header;
      case 5:
        return type::map_header;
      case 6:
        return std::unexpected{error::invalid_input};  // tags not supported
      case 7:
        switch (b & 0x1F) {
          case 20:
          case 21:
            return type::boolean;
          case 22:
            return type::null;
          case 23:
            return type::undefined;
          case 25:
          case 26:
          case 27:
            return type::floating;
          default:
            return std::unexpected{error::invalid_input};
        }
      default:
        return std::unexpected{error::invalid_input};
    }
  }

  /**
   * @brief Read an unsigned integer (major type 0).
   *
   * @return The decoded value on success.
   *
   * @pre None.
   * @post On success the cursor advances past the item; on failure it is
   *       unchanged or advanced only past the consumed head byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation,
   *         \c error::type_mismatch when the next item is not major
   *         type 0, or \c error::invalid_input on a malformed argument.
   */
  [[nodiscard]] auto read_uint() noexcept -> std::expected<std::uint64_t, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if ((b >> 5) != 0)
      return std::unexpected{error::type_mismatch};
    m_cursor.advance(1);
    return read_argument(b & 0x1F);
  }

  /**
   * @brief Read a signed integer (major type 0 or 1).
   *
   * Negative items are mapped back from CBOR's \c -1-n encoding.
   *
   * @return The decoded value on success.
   *
   * @pre None.
   * @post On success the cursor advances past the item; on failure it is
   *       unchanged or advanced only past the consumed head byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the item is neither major type
   *         0 nor 1 or its argument overflows \c int64_t.
   */
  [[nodiscard]] auto read_int() noexcept -> std::expected<std::int64_t, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    auto const mt{static_cast<std::uint8_t>(b >> 5)};
    if (mt != 0 && mt != 1)
      return std::unexpected{error::type_mismatch};
    m_cursor.advance(1);
    auto arg{read_argument(b & 0x1F)};
    if (!arg)
      return std::unexpected{arg.error()};
    if (mt == 0) {
      if (*arg > static_cast<std::uint64_t>(0x7FFFFFFFFFFFFFFFLL)) {
        return std::unexpected{error::type_mismatch};
      }
      return static_cast<std::int64_t>(*arg);
    }
    if (*arg > static_cast<std::uint64_t>(0x7FFFFFFFFFFFFFFFLL)) {
      return std::unexpected{error::type_mismatch};
    }
    return -static_cast<std::int64_t>(*arg) - 1;
  }

  /**
   * @brief Read a UTF-8 text string (major type 3) as a view.
   *
   * Zero-copy: the returned view aliases the source buffer.
   *
   * @return A view of the string body on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header and body; on
   *       failure it is unchanged or advanced only past the head byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not major
   *         type 3.
   *
   * @warning The returned view is invalidated when the source buffer is
   *          destroyed or modified.
   */
  [[nodiscard]] auto read_string() noexcept -> std::expected<std::string_view, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if ((b >> 5) != 3)
      return std::unexpected{error::type_mismatch};
    m_cursor.advance(1);
    auto const n{read_argument(b & 0x1F)};
    if (!n)
      return std::unexpected{n.error()};
    auto const len{detail::length_to_size<size_type>(*n)};
    if (!len)
      return std::unexpected{len.error()};
    if (!m_cursor.has(*len))
      return std::unexpected{error::buffer_underrun};
    auto const sv{std::string_view{reinterpret_cast<char const*>(m_cursor.data()), *len}};
    m_cursor.advance(*len);
    return sv;
  }

  /**
   * @brief Read a byte string (major type 2) as a view.
   *
   * Zero-copy: the returned span aliases the source buffer.
   *
   * @return A span over the byte-string body on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header and body; on
   *       failure it is unchanged or advanced only past the head byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not major
   *         type 2.
   *
   * @warning The returned span is invalidated when the source buffer is
   *          destroyed or modified.
   */
  [[nodiscard]] auto read_bytes() noexcept -> std::expected<std::span<byte_type const>, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if ((b >> 5) != 2)
      return std::unexpected{error::type_mismatch};
    m_cursor.advance(1);
    auto const n{read_argument(b & 0x1F)};
    if (!n)
      return std::unexpected{n.error()};
    auto const len{detail::length_to_size<size_type>(*n)};
    if (!len)
      return std::unexpected{len.error()};
    if (!m_cursor.has(*len))
      return std::unexpected{error::buffer_underrun};
    auto const out{m_cursor.take(*len)};
    return out;
  }

  /**
   * @brief Read an array header (major type 4), returning its length.
   *
   * The caller then reads exactly that many elements.
   *
   * @return The element count on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header; on failure it
   *       is unchanged or advanced only past the head byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not major
   *         type 4.
   */
  [[nodiscard]] auto read_array_header() noexcept -> std::expected<std::uint64_t, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if ((b >> 5) != 4)
      return std::unexpected{error::type_mismatch};
    m_cursor.advance(1);
    return read_argument(b & 0x1F);
  }

  /**
   * @brief Read a map header (major type 5), returning the pair count.
   *
   * The caller then reads exactly that many key/value pairs.
   *
   * @return The number of key-value pairs on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header; on failure it
   *       is unchanged or advanced only past the head byte.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not major
   *         type 5.
   */
  [[nodiscard]] auto read_map_header() noexcept -> std::expected<std::uint64_t, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if ((b >> 5) != 5)
      return std::unexpected{error::type_mismatch};
    m_cursor.advance(1);
    return read_argument(b & 0x1F);
  }

  /**
   * @brief Read a boolean simple value.
   *
   * @return \c false for 0xF4 and \c true for 0xF5.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun at end of input, or
   *         \c error::type_mismatch when the next byte is not a CBOR
   *         boolean.
   */
  [[nodiscard]] auto read_bool() noexcept -> std::expected<bool, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if (b == 0xF4) {
      m_cursor.advance(1);
      return false;
    }
    if (b == 0xF5) {
      m_cursor.advance(1);
      return true;
    }
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Consume a \c null simple value (0xF6).
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::type_mismatch when the next byte is
   *         not 0xF6 (also returned at end of input).
   */
  auto read_null() noexcept -> std::expected<void, error> {
    if (!m_cursor.has(1) || static_cast<std::uint8_t>(m_cursor.data()[0]) != 0xF6) {
      return std::unexpected{error::type_mismatch};
    }
    m_cursor.advance(1);
    return {};
  }

  /**
   * @brief Read a floating-point value as a \c double.
   *
   * Accepts half (0xF9), single (0xFA), and double (0xFB) precision;
   * half and single forms are widened to \c double.
   *
   * @return The decoded value on success.
   *
   * @pre None.
   * @post On success the cursor advances past the item; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not a CBOR
   *         float.
   */
  [[nodiscard]] auto read_float() noexcept -> std::expected<double, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if (b == 0xF9) {  // half precision
      if (!m_cursor.has(3))
        return std::unexpected{error::buffer_underrun};
      m_cursor.advance(1);
      auto const h{detail::load_be16(m_cursor.data())};
      m_cursor.advance(2);
      return detail::half_to_double(h);
    }
    if (b == 0xFA) {
      if (!m_cursor.has(5))
        return std::unexpected{error::buffer_underrun};
      m_cursor.advance(1);
      auto const u{detail::load_be32(m_cursor.data())};
      m_cursor.advance(4);
      return static_cast<double>(std::bit_cast<float>(u));
    }
    if (b == 0xFB) {
      if (!m_cursor.has(9))
        return std::unexpected{error::buffer_underrun};
      m_cursor.advance(1);
      auto const u{detail::load_be64(m_cursor.data())};
      m_cursor.advance(8);
      return std::bit_cast<double>(u);
    }
    return std::unexpected{error::type_mismatch};
  }
};

}  // namespace nexenne::serialization::cbor
