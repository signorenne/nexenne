#pragma once

/**
 * @file
 * @brief CBOR (RFC 8949) writer and reader.
 *
 * Covers the eight major types except tagged values and indefinite lengths.
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
 * Output is deterministic: integers are encoded with the smallest
 * representation, and the writer never emits indefinite-length
 * items. It is not fully canonical: floats keep the width the caller
 * wrote, and map key order is the caller's.
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
 * @brief Logical kind of the next CBOR item, as reported by \c reader::peek_type.
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

/// @cond INTERNAL
namespace detail {

/**
 * @brief Store an unsigned value big-endian at \p dst.
 *
 * The byte order is handled by \c nexenne::utility, which writes the bytes
 * most-significant-first into the fixed-extent destination span.
 *
 * @tparam U Unsigned integral type to store.
 * @param dst Destination for \c sizeof(U) big-endian bytes.
 * @param value Value to store.
 *
 * @pre \p dst points at \c sizeof(U) writable bytes.
 * @post \p dst holds \p value in big-endian order.
 */
template <std::unsigned_integral U>
inline auto store_be(std::byte* const dst, U value) noexcept -> void {
  nexenne::utility::write_be(std::span<std::byte, sizeof(U)>{dst, sizeof(U)}, value);
}

/**
 * @brief Load an unsigned value from big-endian bytes at \p src.
 *
 * @tparam U Unsigned integral type to load.
 * @param src Source of \c sizeof(U) big-endian bytes.
 *
 * @return The decoded value.
 *
 * @pre \p src points at \c sizeof(U) readable bytes.
 * @post None.
 */
template <std::unsigned_integral U>
[[nodiscard]] inline auto load_be(std::byte const* const src) noexcept -> U {
  return nexenne::utility::read_be<U>(std::span<std::byte const, sizeof(U)>{src, sizeof(U)});
}

/**
 * @brief Store a 16-bit value big-endian at \p dst.
 *
 * @param dst Destination for two big-endian bytes.
 * @param v Value to store.
 *
 * @pre \p dst points at two writable bytes.
 * @post \p dst holds \p v in big-endian order.
 */
inline auto store_be16(std::byte* const dst, std::uint16_t const v) noexcept -> void {
  store_be(dst, v);
}

/**
 * @brief Store a 32-bit value big-endian at \p dst.
 *
 * @param dst Destination for four big-endian bytes.
 * @param v Value to store.
 *
 * @pre \p dst points at four writable bytes.
 * @post \p dst holds \p v in big-endian order.
 */
inline auto store_be32(std::byte* const dst, std::uint32_t const v) noexcept -> void {
  store_be(dst, v);
}

/**
 * @brief Store a 64-bit value big-endian at \p dst.
 *
 * @param dst Destination for eight big-endian bytes.
 * @param v Value to store.
 *
 * @pre \p dst points at eight writable bytes.
 * @post \p dst holds \p v in big-endian order.
 */
inline auto store_be64(std::byte* const dst, std::uint64_t const v) noexcept -> void {
  store_be(dst, v);
}

/**
 * @brief Load a 16-bit value from two big-endian bytes at \p src.
 *
 * @param src Source of two big-endian bytes.
 *
 * @return The decoded value.
 *
 * @pre \p src points at two readable bytes.
 * @post None.
 */
[[nodiscard]] inline auto load_be16(std::byte const* const src) noexcept -> std::uint16_t {
  return load_be<std::uint16_t>(src);
}

/**
 * @brief Load a 32-bit value from four big-endian bytes at \p src.
 *
 * @param src Source of four big-endian bytes.
 *
 * @return The decoded value.
 *
 * @pre \p src points at four readable bytes.
 * @post None.
 */
[[nodiscard]] inline auto load_be32(std::byte const* const src) noexcept -> std::uint32_t {
  return load_be<std::uint32_t>(src);
}

/**
 * @brief Load a 64-bit value from eight big-endian bytes at \p src.
 *
 * @param src Source of eight big-endian bytes.
 *
 * @return The decoded value.
 *
 * @pre \p src points at eight readable bytes.
 * @post None.
 */
[[nodiscard]] inline auto load_be64(std::byte const* const src) noexcept -> std::uint64_t {
  return load_be<std::uint64_t>(src);
}

/**
 * @brief Convert an IEEE 754 half-precision bit pattern to a double.
 *
 * Used only on the read path; the writer never emits half floats. Handles
 * signed zero, subnormals (renormalised into single precision), and the
 * infinity / NaN exponent.
 *
 * @param h The 16-bit half-precision bit pattern.
 *
 * @return The value widened to \c double.
 *
 * @pre None.
 * @post None.
 */
inline auto half_to_double(std::uint16_t const h) noexcept -> double {
  auto const exp{static_cast<std::uint32_t>((h >> 10) & 0x1F)};
  auto const mant{static_cast<std::uint32_t>(h & 0x3FF)};
  auto const sign{static_cast<std::uint32_t>((h >> 15) & 1)};
  std::uint32_t f{sign << 31};
  if (exp == 0) {
    if (mant != 0) {
      // Subnormal: renormalise; e starts at -1 so 127 - 15 - e is the right power of two.
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
 *
 * @pre None.
 * @post On success the value equals \p len.
 */
template <std::unsigned_integral SizeT>
[[nodiscard]] constexpr auto length_to_size(std::uint64_t const len) noexcept
  -> std::expected<SizeT, error> {
  if (len > static_cast<std::uint64_t>(std::numeric_limits<SizeT>::max())) {
    return std::unexpected{error::string_too_long};
  }
  return static_cast<SizeT>(len);
}

/**
 * @brief The CBOR major types, the top three bits of a head byte (RFC 8949 section 3.1).
 */
namespace major {

inline constexpr std::uint8_t unsigned_integer{0};  ///< Major type 0: an unsigned integer.
inline constexpr std::uint8_t negative_integer{1};  ///< Major type 1: minus one minus the argument.
inline constexpr std::uint8_t byte_string{2};       ///< Major type 2: a byte string.
inline constexpr std::uint8_t text_string{3};       ///< Major type 3: a UTF-8 text string.
inline constexpr std::uint8_t array{4};             ///< Major type 4: an array of items.
inline constexpr std::uint8_t map{5};               ///< Major type 5: a map of item pairs.
inline constexpr std::uint8_t tag{6};               ///< Major type 6: a tagged item, unsupported.
inline constexpr std::uint8_t simple{7};            ///< Major type 7: simple values and floats.

}  // namespace major

/**
 * @brief The additional-information values that size an argument (RFC 8949 section 3).
 */
namespace info {

inline constexpr std::uint8_t direct_max{23};   ///< Largest argument held in the head byte itself.
inline constexpr std::uint8_t one_byte{24};     ///< A one-byte argument follows.
inline constexpr std::uint8_t two_bytes{25};    ///< A two-byte argument follows.
inline constexpr std::uint8_t four_bytes{26};   ///< A four-byte argument follows.
inline constexpr std::uint8_t eight_bytes{27};  ///< An eight-byte argument follows.

}  // namespace info

/**
 * @brief The major type 7 values this codec reads and writes (RFC 8949 section 3.3).
 */
namespace simple {

inline constexpr std::uint8_t false_value{20};   ///< Boolean false.
inline constexpr std::uint8_t true_value{21};    ///< Boolean true.
inline constexpr std::uint8_t null{22};          ///< The null value.
inline constexpr std::uint8_t undefined{23};     ///< The undefined value.
inline constexpr std::uint8_t one_byte{24};      ///< A simple value in the following byte.
inline constexpr std::uint8_t half_float{25};    ///< IEEE 754 half precision.
inline constexpr std::uint8_t single_float{26};  ///< IEEE 754 single precision.
inline constexpr std::uint8_t double_float{27};  ///< IEEE 754 double precision.

}  // namespace simple

/**
 * @brief The head byte of a major type 7 value.
 *
 * @param value One of the \c simple values.
 *
 * @return The byte that carries major type 7 and \p value.
 *
 * @pre \p value is below 32.
 * @post None.
 */
[[nodiscard]] constexpr auto simple_head(std::uint8_t const value) noexcept -> std::uint8_t {
  return static_cast<std::uint8_t>((major::simple << 5) | value);
}

}  // namespace detail

/// @endcond

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
  using byte_type = std::byte;    ///< Byte type of the buffer.
  using size_type = std::size_t;  ///< Size and offset type.

private:
  nexenne::utility::buffer_cursor<byte_type> m_cursor;

  /**
   * @brief Whether a head plus a body fit the remaining space.
   *
   * Computed without overflowing \c size_type: \c head plus \c body could wrap
   * on a 32-bit target with a huge body, so the check subtracts instead of
   * adding.
   *
   * @param head Size of the item head in bytes.
   * @param body Size of the item body in bytes.
   *
   * @return \c true when both the head and the body fit in the space left.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto
  fits_prefixed(size_type const head, size_type const body) const noexcept -> bool {
    auto const remaining{m_cursor.remaining()};
    return head <= remaining && body <= remaining - head;
  }

  /**
   * @brief Number of bytes \c write_head emits for argument \p v.
   *
   * Mirrors the 1/2/3/5/9-byte forms of \c write_head so \c write_bytes and
   * \c write_string can pre-check the head plus the body.
   *
   * @param v The length or value argument to be encoded.
   *
   * @return The head size in bytes: 1, 2, 3, 5, or 9.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto head_size(std::uint64_t const v) noexcept -> size_type {
    if (v <= detail::info::direct_max)
      return 1;
    if (v <= 0xFF)
      return 2;
    if (v <= 0xFFFF)
      return 3;
    if (v <= 0xFFFFFFFFu)
      return 5;
    return 9;
  }

  /**
   * @brief Write a major-type byte and its length argument.
   *
   * Emits the 3-bit major type in the head byte, then the argument \p v in
   * CBOR's compact 0/1/2/4/8-byte encoding, choosing the smallest form.
   *
   * @param major The 3-bit CBOR major type (0 to 7).
   * @param v The length or value argument to encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by the encoded head size; on failure
   *       it is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the head does not fit.
   */
  [[nodiscard]] auto write_head(std::uint8_t const major, std::uint64_t const v) noexcept
    -> std::expected<void, error> {
    auto const m{static_cast<std::uint8_t>(major << 5)};
    if (v <= detail::info::direct_max) {
      if (!m_cursor.has(1))
        return std::unexpected{error::buffer_full};
      m_cursor.put(static_cast<byte_type>(m | static_cast<std::uint8_t>(v)));
      return {};
    }
    if (v <= 0xFF) {
      if (!m_cursor.has(2))
        return std::unexpected{error::buffer_full};
      m_cursor.put(static_cast<byte_type>(m | detail::info::one_byte));
      m_cursor.put(static_cast<byte_type>(v));
      return {};
    }
    if (v <= 0xFFFF) {
      if (!m_cursor.has(3))
        return std::unexpected{error::buffer_full};
      m_cursor.put(static_cast<byte_type>(m | detail::info::two_bytes));
      detail::store_be16(m_cursor.data(), static_cast<std::uint16_t>(v));
      m_cursor.advance(2);
      return {};
    }
    if (v <= 0xFFFFFFFFu) {
      if (!m_cursor.has(5))
        return std::unexpected{error::buffer_full};
      m_cursor.put(static_cast<byte_type>(m | detail::info::four_bytes));
      detail::store_be32(m_cursor.data(), static_cast<std::uint32_t>(v));
      m_cursor.advance(4);
      return {};
    }
    if (!m_cursor.has(9))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(m | detail::info::eight_bytes));
    detail::store_be64(m_cursor.data(), v);
    m_cursor.advance(8);
    return {};
  }

public:
  /**
   * @brief Construct a writer over the mutable byte span \p buf.
   *
   * @param buf Destination bytes to fill. Must outlive the writer.
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
   * @param v Unsigned value to encode.
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
  [[nodiscard]] auto write_uint(std::uint64_t const v) noexcept -> std::expected<void, error> {
    return write_head(detail::major::unsigned_integer, v);
  }

  /**
   * @brief Encode a signed integer, choosing major type 0 or 1.
   *
   * Non-negative values use major type 0; negative values use major
   * type 1, which encodes \c -1-n as argument \c n. The full \c int64_t
   * range is representable.
   *
   * @param v Signed value to encode.
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
  [[nodiscard]] auto write_int(std::int64_t const v) noexcept -> std::expected<void, error> {
    if (v >= 0)
      return write_head(detail::major::unsigned_integer, static_cast<std::uint64_t>(v));
    return write_head(detail::major::negative_integer, static_cast<std::uint64_t>(-(v + 1)));
  }

  /**
   * @brief Encode a byte string (major type 2).
   *
   * Writes the length header followed by the raw bytes.
   *
   * @param data Bytes to encode.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header and body; on
   *       failure it is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the header or body
   *         does not fit.
   */
  [[nodiscard]] auto write_bytes(std::span<byte_type const> const data) noexcept
    -> std::expected<void, error> {
    if (!fits_prefixed(head_size(data.size()), data.size()))
      return std::unexpected{error::buffer_full};
    if (auto const r{write_head(detail::major::byte_string, data.size())}; !r) [[unlikely]]
      return r;
    // memcpy with a null pointer is UB even for size 0.
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
   * @param s Text to encode; expected to be valid UTF-8, which the
   *          writer does not verify.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances past the header and body; on
   *       failure it is unchanged.
   *
   * @throws None. Returns \c error::buffer_full when the header or body
   *         does not fit.
   */
  [[nodiscard]] auto write_string(std::string_view const s) noexcept -> std::expected<void, error> {
    if (!fits_prefixed(head_size(s.size()), s.size()))
      return std::unexpected{error::buffer_full};
    if (auto const r{write_head(detail::major::text_string, s.size())}; !r) [[unlikely]]
      return r;
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
   * @param n Number of array elements that will follow.
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
  [[nodiscard]] auto write_array_header(std::uint64_t const n) noexcept
    -> std::expected<void, error> {
    return write_head(detail::major::array, n);
  }

  /**
   * @brief Encode a map header of \p n key-value pairs (major type 5).
   *
   * The caller must then write exactly \p n key/value item pairs.
   *
   * @param n Number of key-value pairs that will follow.
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
  [[nodiscard]] auto write_map_header(std::uint64_t const n) noexcept
    -> std::expected<void, error> {
    return write_head(detail::major::map, n);
  }

  /**
   * @brief Encode a boolean simple value.
   *
   * @param v Boolean to write as CBOR \c true (0xF5) or \c false
   *          (0xF4).
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::buffer_full when no byte remains.
   */
  [[nodiscard]] auto write_bool(bool const v) noexcept -> std::expected<void, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_full};
    m_cursor.put(
      static_cast<byte_type>(
        v ? detail::simple_head(detail::simple::true_value)
          : detail::simple_head(detail::simple::false_value)
      )
    );
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
  [[nodiscard]] auto write_null() noexcept -> std::expected<void, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(detail::simple_head(detail::simple::null)));
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
  [[nodiscard]] auto write_undefined() noexcept -> std::expected<void, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(detail::simple_head(detail::simple::undefined)));
    return {};
  }

  /**
   * @brief Encode a single-precision float (0xFA prefix).
   *
   * @param v Value to encode as IEEE-754 float32 in big-endian order.
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
  [[nodiscard]] auto write_float32(float const v) noexcept -> std::expected<void, error> {
    if (!m_cursor.has(5))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(detail::simple_head(detail::simple::single_float)));
    detail::store_be32(m_cursor.data(), std::bit_cast<std::uint32_t>(v));
    m_cursor.advance(4);
    return {};
  }

  /**
   * @brief Encode a double-precision float (0xFB prefix).
   *
   * @param v Value to encode as IEEE-754 float64 in big-endian order.
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
  [[nodiscard]] auto write_float64(double const v) noexcept -> std::expected<void, error> {
    if (!m_cursor.has(9))
      return std::unexpected{error::buffer_full};
    m_cursor.put(static_cast<byte_type>(detail::simple_head(detail::simple::double_float)));
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
  using byte_type = std::byte;    ///< Byte type of the buffer.
  using size_type = std::size_t;  ///< Size and offset type.

private:
  nexenne::utility::buffer_cursor<byte_type const> m_cursor;

  /**
   * @brief Decode the length / value argument that follows a head byte.
   *
   * The 5-bit additional-info field \p ai either carries the argument directly
   * (values below 24) or selects a 1/2/4/8-byte big-endian argument that
   * follows, which this reads and advances past.
   *
   * @param ai The 5-bit additional-info field of the head byte.
   *
   * @return The decoded argument on success.
   *
   * @pre None.
   * @post On success the cursor advances past any argument bytes; on failure
   *       it is unchanged.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::invalid_input for a reserved additional-info value.
   */
  [[nodiscard]] auto read_argument(std::uint8_t const ai) noexcept
    -> std::expected<std::uint64_t, error> {
    if (ai <= detail::info::direct_max)
      return ai;
    switch (ai) {
      case detail::info::one_byte:
        if (!m_cursor.has(1))
          return std::unexpected{error::buffer_underrun};
        return static_cast<std::uint64_t>(static_cast<std::uint8_t>(m_cursor.next()));
      case detail::info::two_bytes:
        if (!m_cursor.has(2))
          return std::unexpected{error::buffer_underrun};
        {
          auto v{detail::load_be16(m_cursor.data())};
          m_cursor.advance(2);
          return static_cast<std::uint64_t>(v);
        }
      case detail::info::four_bytes:
        if (!m_cursor.has(4))
          return std::unexpected{error::buffer_underrun};
        {
          auto v{detail::load_be32(m_cursor.data())};
          m_cursor.advance(4);
          return static_cast<std::uint64_t>(v);
        }
      case detail::info::eight_bytes:
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
   * @param buf Source bytes to decode. Must outlive the reader and any
   *            view it returns.
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
      case detail::major::unsigned_integer:
        return type::unsigned_int;
      case detail::major::negative_integer:
        return type::negative_int;
      case detail::major::byte_string:
        return type::byte_string;
      case detail::major::text_string:
        return type::text_string;
      case detail::major::array:
        return type::array_header;
      case detail::major::map:
        return type::map_header;
      case detail::major::tag:
        return std::unexpected{error::invalid_input};
      case detail::major::simple:
        switch (b & 0x1F) {
          case detail::simple::false_value:
          case detail::simple::true_value:
            return type::boolean;
          case detail::simple::null:
            return type::null;
          case detail::simple::undefined:
            return type::undefined;
          case detail::simple::half_float:
          case detail::simple::single_float:
          case detail::simple::double_float:
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
    if ((b >> 5) != detail::major::unsigned_integer)
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
    if (mt != detail::major::unsigned_integer && mt != detail::major::negative_integer)
      return std::unexpected{error::type_mismatch};
    m_cursor.advance(1);
    auto arg{read_argument(b & 0x1F)};
    if (!arg)
      return std::unexpected{arg.error()};
    if (mt == detail::major::unsigned_integer) {
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
   *       failure it is unchanged or advanced past the head byte and any length bytes it read.
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
    if ((b >> 5) != detail::major::text_string)
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
   *       failure it is unchanged or advanced past the head byte and any length bytes it read.
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
    if ((b >> 5) != detail::major::byte_string)
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
   *       is unchanged or advanced past the head byte and any length bytes it read.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not major
   *         type 4.
   */
  [[nodiscard]] auto read_array_header() noexcept -> std::expected<std::uint64_t, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if ((b >> 5) != detail::major::array)
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
   *       is unchanged or advanced past the head byte and any length bytes it read.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::type_mismatch when the next item is not major
   *         type 5.
   */
  [[nodiscard]] auto read_map_header() noexcept -> std::expected<std::uint64_t, error> {
    if (!m_cursor.has(1))
      return std::unexpected{error::buffer_underrun};
    auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
    if ((b >> 5) != detail::major::map)
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
    if (b == detail::simple_head(detail::simple::false_value)) {
      m_cursor.advance(1);
      return false;
    }
    if (b == detail::simple_head(detail::simple::true_value)) {
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
  [[nodiscard]] auto read_null() noexcept -> std::expected<void, error> {
    if (!m_cursor.has(1)
        || static_cast<std::uint8_t>(m_cursor.data()[0])
             != detail::simple_head(detail::simple::null)) {
      return std::unexpected{error::type_mismatch};
    }
    m_cursor.advance(1);
    return {};
  }

  /**
   * @brief Consume an \c undefined simple value (0xF7).
   *
   * Mirrors \c read_null so a caller can advance past the \c undefined
   * marker a peer CBOR producer may emit (for example an omitted map
   * field), which \c read_null rejects.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor advances by one byte; on failure it is
   *       unchanged.
   *
   * @throws None. Returns \c error::type_mismatch when the next byte is
   *         not 0xF7 (also returned at end of input).
   */
  [[nodiscard]] auto read_undefined() noexcept -> std::expected<void, error> {
    if (!m_cursor.has(1)
        || static_cast<std::uint8_t>(m_cursor.data()[0])
             != detail::simple_head(detail::simple::undefined)) {
      return std::unexpected{error::type_mismatch};
    }
    m_cursor.advance(1);
    return {};
  }

  /**
   * @brief Skip exactly one complete item of any supported type.
   *
   * Advances past the next data item, recursing conceptually into arrays
   * and maps so nested containers are consumed whole. The walk is
   * iterative (a pending-item counter, not call recursion), so a hostile
   * deeply nested stream cannot overflow the stack.
   *
   * @return Empty on success.
   *
   * @pre None.
   * @post On success the cursor sits just past the skipped item; on
   *       failure it may have advanced over part of the item.
   *
   * @throws None. Returns \c error::buffer_underrun on truncation, or
   *         \c error::invalid_input for an unsupported major type (a tag)
   *         or an unrecognised simple value.
   *
   * @complexity \c O(size of the skipped item).
   */
  [[nodiscard]] auto skip_value() noexcept -> std::expected<void, error> {
    auto pending{std::uint64_t{1}};
    while (pending > 0) {
      if (!m_cursor.has(1))
        return std::unexpected{error::buffer_underrun};
      auto const b{static_cast<std::uint8_t>(m_cursor.data()[0])};
      auto const mt{static_cast<std::uint8_t>(b >> 5)};
      auto const ai{static_cast<std::uint8_t>(b & 0x1F)};
      m_cursor.advance(1);
      --pending;
      switch (mt) {
        case detail::major::unsigned_integer:
        case detail::major::negative_integer: {
          if (auto const arg{read_argument(ai)}; !arg)
            return std::unexpected{arg.error()};
          break;
        }
        case detail::major::byte_string:
        case detail::major::text_string: {
          auto const arg{read_argument(ai)};
          if (!arg)
            return std::unexpected{arg.error()};
          auto const len{detail::length_to_size<size_type>(*arg)};
          if (!len)
            return std::unexpected{len.error()};
          if (!m_cursor.has(*len))
            return std::unexpected{error::buffer_underrun};
          m_cursor.advance(*len);
          break;
        }
        case detail::major::array: {
          auto const arg{read_argument(ai)};
          if (!arg)
            return std::unexpected{arg.error()};
          // A counter overflow means more items than bytes: report an underrun.
          if (*arg > std::numeric_limits<std::uint64_t>::max() - pending)
            return std::unexpected{error::buffer_underrun};
          pending += *arg;
          break;
        }
        case detail::major::map: {
          auto const arg{read_argument(ai)};
          if (!arg)
            return std::unexpected{arg.error()};
          if (*arg > std::numeric_limits<std::uint64_t>::max() / 2)
            return std::unexpected{error::buffer_underrun};
          auto const items{*arg * 2};
          if (items > std::numeric_limits<std::uint64_t>::max() - pending)
            return std::unexpected{error::buffer_underrun};
          pending += items;
          break;
        }
        case detail::major::simple: {
          switch (ai) {
            case detail::simple::false_value:
            case detail::simple::true_value:
            case detail::simple::null:
            case detail::simple::undefined:
              break;
            case detail::simple::one_byte:
              if (!m_cursor.has(1))
                return std::unexpected{error::buffer_underrun};
              m_cursor.advance(1);
              break;
            case detail::simple::half_float:
              if (!m_cursor.has(2))
                return std::unexpected{error::buffer_underrun};
              m_cursor.advance(2);
              break;
            case detail::simple::single_float:
              if (!m_cursor.has(4))
                return std::unexpected{error::buffer_underrun};
              m_cursor.advance(4);
              break;
            case detail::simple::double_float:
              if (!m_cursor.has(8))
                return std::unexpected{error::buffer_underrun};
              m_cursor.advance(8);
              break;
            default:
              return std::unexpected{error::invalid_input};
          }
          break;
        }
        default:
          return std::unexpected{error::invalid_input};
      }
    }
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
    if (b == detail::simple_head(detail::simple::half_float)) {
      if (!m_cursor.has(3))
        return std::unexpected{error::buffer_underrun};
      m_cursor.advance(1);
      auto const h{detail::load_be16(m_cursor.data())};
      m_cursor.advance(2);
      return detail::half_to_double(h);
    }
    if (b == detail::simple_head(detail::simple::single_float)) {
      if (!m_cursor.has(5))
        return std::unexpected{error::buffer_underrun};
      m_cursor.advance(1);
      auto const u{detail::load_be32(m_cursor.data())};
      m_cursor.advance(4);
      return static_cast<double>(std::bit_cast<float>(u));
    }
    if (b == detail::simple_head(detail::simple::double_float)) {
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
