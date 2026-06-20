#pragma once

/**
 * @file
 * @brief Generic table-driven CRC engine, parameterised by the Rocksoft model.
 *
 * Every CRC in common use is described by the six-parameter Rocksoft model:
 * the register width, the polynomial (without its leading 1), the initial
 * register value, whether each input byte is reflected (LSB-first), whether
 * the final register is reflected before the output XOR, and that XOR value.
 * The catalogue at https://reveng.sourceforge.io/crc-catalogue/all.htm lists
 * around a hundred named CRCs, each one a single \c crc_spec instantiation.
 *
 * One 256-entry lookup table is generated per spec at \c constexpr time, so it
 * is fixed size (256, 512, or 1024 bytes) and ROM-resident on an MCU. The whole
 * engine is \c constexpr and \c noexcept with no heap, recursion, or exceptions.
 * Named presets for the widely used CRC-8, CRC-16, and CRC-32 variants, plus
 * \c crc8, \c crc32, and \c crc32c convenience wrappers, are provided below.
 *
 * Not cryptographically secure.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace nexenne::algorithm {

namespace detail {

// The smallest unsigned integer type able to hold a Width-bit CRC.
template <std::size_t Width>
using crc_uint_t = std::conditional_t<
  Width <= 8,
  std::uint8_t,
  std::conditional_t<
    Width <= 16,
    std::uint16_t,
    std::conditional_t<Width <= 32, std::uint32_t, std::uint64_t>>>;

// Reverses the low n_bits bits of value (bit 0 with bit n_bits-1, and so on).
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto reflect_bits(T const value, std::size_t const n_bits) noexcept -> T {
  auto out{T{0}};
  for (auto i{std::size_t{0}}; i < n_bits; ++i) {
    if ((value >> i) & T{1}) {
      out |= static_cast<T>(T{1} << (n_bits - 1 - i));
    }
  }
  return out;
}

// A mask of the low Width bits of T (all bits when Width fills the type,
// avoiding an undefined shift by the full width).
template <std::unsigned_integral T, std::size_t Width>
inline constexpr auto crc_mask{
  (Width == sizeof(T) * 8) ? static_cast<T>(~T{0}) : static_cast<T>((T{1} << Width) - 1)
};

}  // namespace detail

/**
 * @brief Compile-time description of one named CRC algorithm.
 *
 * Pass as a non-type template parameter to \c crc or \c crc_ctx. The width is
 * the template parameter; the remaining Rocksoft fields are data members so
 * a spec reads as a designated-initialiser literal.
 *
 * @tparam WidthBits Register width in bits.
 */
template <std::size_t WidthBits>
struct crc_spec {
  using value_type = detail::crc_uint_t<WidthBits>;
  static constexpr std::size_t width{WidthBits};

  value_type poly{};     ///< Polynomial without its leading 1.
  value_type init{};     ///< Initial register value.
  bool ref_in{};         ///< Reflect each input byte (LSB-first processing).
  bool ref_out{};        ///< Reflect the register before the output XOR.
  value_type xor_out{};  ///< Value XOR'd with the final register.
};

namespace detail {

// Builds the 256-entry byte-at-a-time lookup table for Spec.
template <crc_spec Spec>
[[nodiscard]] constexpr auto
make_crc_table() noexcept -> std::array<typename decltype(Spec)::value_type, 256> {
  using value_type = typename decltype(Spec)::value_type;
  constexpr auto width{Spec.width};
  constexpr auto mask{crc_mask<value_type, width>};
  auto table{std::array<value_type, 256>{}};

  if constexpr (Spec.ref_in) {
    // Reflected (LSB-first) table: shift right with the reflected polynomial.
    auto const rev_poly{reflect_bits<value_type>(Spec.poly, width)};
    for (auto i{std::size_t{0}}; i < 256; ++i) {
      auto reg{static_cast<value_type>(i)};
      for (auto b{0}; b < 8; ++b) {
        reg = (reg & value_type{1}) ? static_cast<value_type>((reg >> 1) ^ rev_poly)
                                    : static_cast<value_type>(reg >> 1);
      }
      table[i] = static_cast<value_type>(reg & mask);
    }
  } else {
    // The non-reflected (MSB-first) path shifts bytes to the top of the register
    // and so needs width >= 8; sub-byte CRCs are only meaningful reflected (the
    // ref_in branch above is width-safe). Reject the unsupported combination with
    // a clear message instead of underflowing width - 8.
    static_assert(width >= 8, "non-reflected CRC requires a width of at least 8 bits");
    // Non-reflected (MSB-first) table: shift left with the polynomial, each
    // byte placed at the top of the register.
    constexpr auto top_bit{static_cast<value_type>(value_type{1} << (width - 1))};
    constexpr auto shift_up{width - 8};
    for (auto i{std::size_t{0}}; i < 256; ++i) {
      auto reg{static_cast<value_type>(static_cast<value_type>(i) << shift_up)};
      for (auto b{0}; b < 8; ++b) {
        reg = (reg & top_bit) ? static_cast<value_type>((reg << 1) ^ Spec.poly)
                              : static_cast<value_type>(reg << 1);
      }
      table[i] = static_cast<value_type>(reg & mask);
    }
  }
  return table;
}

// The lookup table for Spec, materialised once at compile time.
template <crc_spec Spec>
inline constexpr auto crc_table_for{make_crc_table<Spec>()};

}  // namespace detail

/**
 * @brief Computes the CRC of a byte span under the algorithm \p Spec.
 *
 * Runs the table-driven CRC over \p data, applying the preset initial value,
 * input and output reflection, and final XOR. The lookup table is built once
 * per \p Spec at compile time.
 *
 * @tparam Spec A named preset below or a custom \c crc_spec.
 * @param data Bytes to checksum. An empty span yields the value the preset
 *        assigns to empty input.
 *
 * @return The CRC of \p data, masked to the spec width.
 *
 * @pre None.
 * @post Equal inputs under the same \p Spec always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p data.
 *
 * @warning Not cryptographically secure.
 */
template <crc_spec Spec>
[[nodiscard]] constexpr auto crc(std::span<std::uint8_t const> const data) noexcept ->
  typename decltype(Spec)::value_type {
  using value_type = typename decltype(Spec)::value_type;
  constexpr auto width{Spec.width};
  constexpr auto mask{detail::crc_mask<value_type, width>};
  constexpr auto& table{detail::crc_table_for<Spec>};

  if constexpr (Spec.ref_in) {
    auto reg{detail::reflect_bits<value_type>(Spec.init, width)};
    for (auto const b : data) {
      auto const idx{static_cast<std::uint8_t>((reg ^ static_cast<value_type>(b)) & 0xFFu)};
      reg = static_cast<value_type>((reg >> 8) ^ table[idx]);
    }
    if constexpr (!Spec.ref_out) {
      reg = detail::reflect_bits<value_type>(reg, width);
    }
    return static_cast<value_type>((reg ^ Spec.xor_out) & mask);
  } else {
    auto reg{Spec.init};
    constexpr auto shift_down{width - 8};
    for (auto const b : data) {
      auto const idx{static_cast<std::uint8_t>(((reg >> shift_down) ^ b) & 0xFFu)};
      reg = static_cast<value_type>(((reg << 8) ^ table[idx]) & mask);
    }
    if constexpr (Spec.ref_out) {
      reg = detail::reflect_bits<value_type>(reg, width);
    }
    return static_cast<value_type>((reg ^ Spec.xor_out) & mask);
  }
}

/**
 * @brief Computes the CRC of a string view under the algorithm \p Spec.
 *
 * Reinterprets the characters of \p s as bytes and forwards to the byte-span
 * overload.
 *
 * @tparam Spec A named preset below or a custom \c crc_spec.
 * @param s Characters to checksum.
 *
 * @return The CRC of \p s, masked to the spec width.
 *
 * @pre None.
 * @post Equal inputs under the same \p Spec always produce the same value, and
 *       the result matches the byte-span overload over the same bytes.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 *
 * @warning Not cryptographically secure.
 */
template <crc_spec Spec>
[[nodiscard]] constexpr auto crc(std::string_view const s) noexcept ->
  typename decltype(Spec)::value_type {
  return crc<Spec>(
    std::span<std::uint8_t const>{reinterpret_cast<std::uint8_t const*>(s.data()), s.size()}
  );
}

/**
 * @brief Streaming CRC context: feed bytes via \c update, finalize with
 *        \c value.
 *
 * Use for data arriving in chunks (file streams, DMA buffers, network frames).
 * Call \c update any number of times, then \c value; \c reset reuses the
 * context for a new message. The finalized result equals the one-shot \c crc
 * of the concatenated input.
 *
 * @tparam Spec A named preset below or a custom \c crc_spec.
 */
template <crc_spec Spec>
class crc_ctx {
public:
  using value_type = typename decltype(Spec)::value_type;
  static constexpr std::size_t width{Spec.width};

private:
  static constexpr auto mask{detail::crc_mask<value_type, width>};

  [[nodiscard]] static constexpr auto initial_reg() noexcept -> value_type {
    if constexpr (Spec.ref_in) {
      return detail::reflect_bits<value_type>(Spec.init, width);
    } else {
      return Spec.init;
    }
  }

  value_type m_reg{initial_reg()};

public:
  /**
   * @brief Constructs a context holding the preset's initial register.
   *
   * @pre None.
   * @post \c value() returns the CRC of the empty message.
   */
  constexpr crc_ctx() noexcept = default;

  /**
   * @brief Resets the context, discarding all bytes fed.
   *
   * @pre None.
   * @post The context is in the same state as a freshly constructed one.
   */
  constexpr auto reset() noexcept -> void {
    m_reg = initial_reg();
  }

  /**
   * @brief Feeds a byte span into the running CRC.
   *
   * @param data Bytes to append to the stream. An empty span is a no-op.
   *
   * @pre None.
   * @post The register reflects every byte fed since the last reset, in order.
   *
   * @complexity \c O(N) in the size \c N of \p data.
   */
  constexpr auto update(std::span<std::uint8_t const> const data) noexcept -> void {
    constexpr auto& table{detail::crc_table_for<Spec>};
    if constexpr (Spec.ref_in) {
      for (auto const b : data) {
        auto const idx{static_cast<std::uint8_t>((m_reg ^ static_cast<value_type>(b)) & 0xFFu)};
        m_reg = static_cast<value_type>((m_reg >> 8) ^ table[idx]);
      }
    } else {
      constexpr auto shift_down{width - 8};
      for (auto const b : data) {
        auto const idx{static_cast<std::uint8_t>(((m_reg >> shift_down) ^ b) & 0xFFu)};
        m_reg = static_cast<value_type>(((m_reg << 8) ^ table[idx]) & mask);
      }
    }
  }

  /**
   * @brief Feeds the characters of a string view into the running CRC.
   *
   * @param s Characters to append to the stream. An empty view is a no-op.
   *
   * @pre None.
   * @post The register reflects every character fed since the last reset.
   *
   * @complexity \c O(N) in the length \c N of \p s.
   */
  constexpr auto update(std::string_view const s) noexcept -> void {
    update(std::span<std::uint8_t const>{reinterpret_cast<std::uint8_t const*>(s.data()), s.size()}
    );
  }

  /**
   * @brief Returns the CRC of all bytes fed so far.
   *
   * Applies output reflection and the final XOR to a copy of the register, so
   * the context is unchanged and may keep accumulating afterwards.
   *
   * @return The CRC of all bytes accumulated since construction or the last
   *         reset.
   *
   * @pre None.
   * @post The context is unchanged; the result equals the one-shot \c crc of
   *       the concatenated input.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    auto r{m_reg};
    if constexpr (Spec.ref_in != Spec.ref_out) {
      r = detail::reflect_bits<value_type>(r, width);
    }
    return static_cast<value_type>((r ^ Spec.xor_out) & mask);
  }
};

inline constexpr auto crc8_ccitt_spec{
  crc_spec<8>{.poly = 0x07, .init = 0x00, .ref_in = false, .ref_out = false, .xor_out = 0x00}
};
inline constexpr auto crc8_smbus_spec{crc8_ccitt_spec};
inline constexpr auto crc8_rohc_spec{
  crc_spec<8>{.poly = 0x07, .init = 0xFF, .ref_in = true, .ref_out = true, .xor_out = 0x00}
};
inline constexpr auto crc8_dallas_1wire_spec{
  crc_spec<8>{.poly = 0x31, .init = 0x00, .ref_in = true, .ref_out = true, .xor_out = 0x00}
};
inline constexpr auto crc8_autosar_spec{
  crc_spec<8>{.poly = 0x2F, .init = 0xFF, .ref_in = false, .ref_out = false, .xor_out = 0xFF}
};
inline constexpr auto crc8_j1850_spec{
  crc_spec<8>{.poly = 0x1D, .init = 0xFF, .ref_in = false, .ref_out = false, .xor_out = 0xFF}
};
inline constexpr auto crc8_icode_spec{
  crc_spec<8>{.poly = 0x1D, .init = 0xFD, .ref_in = false, .ref_out = false, .xor_out = 0x00}
};

inline constexpr auto crc16_xmodem_spec{
  crc_spec<16>{.poly = 0x1021, .init = 0x0000, .ref_in = false, .ref_out = false, .xor_out = 0x0000}
};
inline constexpr auto crc16_kermit_spec{
  crc_spec<16>{.poly = 0x1021, .init = 0x0000, .ref_in = true, .ref_out = true, .xor_out = 0x0000}
};
inline constexpr auto crc16_modbus_spec{
  crc_spec<16>{.poly = 0x8005, .init = 0xFFFF, .ref_in = true, .ref_out = true, .xor_out = 0x0000}
};
inline constexpr auto crc16_usb_spec{
  crc_spec<16>{.poly = 0x8005, .init = 0xFFFF, .ref_in = true, .ref_out = true, .xor_out = 0xFFFF}
};
inline constexpr auto crc16_x25_spec{
  crc_spec<16>{.poly = 0x1021, .init = 0xFFFF, .ref_in = true, .ref_out = true, .xor_out = 0xFFFF}
};
/// @brief CRC-16/IBM-3740, also known as CRC-16/CCITT-FALSE.
inline constexpr auto crc16_ibm3740_spec{
  crc_spec<16>{.poly = 0x1021, .init = 0xFFFF, .ref_in = false, .ref_out = false, .xor_out = 0x0000}
};

/// @brief CRC-32/ISO-HDLC: IEEE 802.3, zlib, and PNG.
inline constexpr auto crc32_ieee_spec{crc_spec<32>{
  .poly = 0x04C11DB7, .init = 0xFFFFFFFF, .ref_in = true, .ref_out = true, .xor_out = 0xFFFFFFFF
}};
/// @brief CRC-32C (Castagnoli): iSCSI, SCTP, and many filesystems.
inline constexpr auto crc32c_spec{crc_spec<32>{
  .poly = 0x1EDC6F41, .init = 0xFFFFFFFF, .ref_in = true, .ref_out = true, .xor_out = 0xFFFFFFFF
}};
inline constexpr auto crc32_bzip2_spec{crc_spec<32>{
  .poly = 0x04C11DB7, .init = 0xFFFFFFFF, .ref_in = false, .ref_out = false, .xor_out = 0xFFFFFFFF
}};
inline constexpr auto crc32_mpeg2_spec{crc_spec<32>{
  .poly = 0x04C11DB7, .init = 0xFFFFFFFF, .ref_in = false, .ref_out = false, .xor_out = 0x00000000
}};

/**
 * @brief CRC-8/CCITT (SMBus) checksum of a byte span.
 *
 * @param d Bytes to checksum.
 *
 * @return The CRC-8/CCITT of \p d.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p d.
 */
[[nodiscard]] constexpr auto crc8(std::span<std::uint8_t const> const d) noexcept -> std::uint8_t {
  return crc<crc8_ccitt_spec>(d);
}

/**
 * @brief CRC-8/CCITT (SMBus) checksum of a string view.
 *
 * @param s Characters to checksum.
 *
 * @return The CRC-8/CCITT of \p s.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 */
[[nodiscard]] constexpr auto crc8(std::string_view const s) noexcept -> std::uint8_t {
  return crc<crc8_ccitt_spec>(s);
}

/**
 * @brief CRC-8/Dallas 1-Wire (Maxim-DOW) checksum of a byte span.
 *
 * @param d Bytes to checksum.
 *
 * @return The CRC-8/Dallas 1-Wire of \p d.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p d.
 */
[[nodiscard]] constexpr auto crc8_1wire(std::span<std::uint8_t const> const d
) noexcept -> std::uint8_t {
  return crc<crc8_dallas_1wire_spec>(d);
}

/**
 * @brief CRC-8/Dallas 1-Wire (Maxim-DOW) checksum of a string view.
 *
 * @param s Characters to checksum.
 *
 * @return The CRC-8/Dallas 1-Wire of \p s.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 */
[[nodiscard]] constexpr auto crc8_1wire(std::string_view const s) noexcept -> std::uint8_t {
  return crc<crc8_dallas_1wire_spec>(s);
}

/**
 * @brief CRC-32/ISO-HDLC checksum of a byte span (IEEE 802.3, zlib, PNG).
 *
 * @param d Bytes to checksum.
 *
 * @return The CRC-32 of \p d.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p d.
 */
[[nodiscard]] constexpr auto crc32(std::span<std::uint8_t const> const d
) noexcept -> std::uint32_t {
  return crc<crc32_ieee_spec>(d);
}

/**
 * @brief CRC-32/ISO-HDLC checksum of a string view (IEEE 802.3, zlib, PNG).
 *
 * @param s Characters to checksum.
 *
 * @return The CRC-32 of \p s.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 */
[[nodiscard]] constexpr auto crc32(std::string_view const s) noexcept -> std::uint32_t {
  return crc<crc32_ieee_spec>(s);
}

/**
 * @brief CRC-32C (Castagnoli) checksum of a byte span.
 *
 * @param d Bytes to checksum.
 *
 * @return The CRC-32C of \p d.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p d.
 */
[[nodiscard]] constexpr auto crc32c(std::span<std::uint8_t const> const d
) noexcept -> std::uint32_t {
  return crc<crc32c_spec>(d);
}

/**
 * @brief CRC-32C (Castagnoli) checksum of a string view.
 *
 * @param s Characters to checksum.
 *
 * @return The CRC-32C of \p s.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 */
[[nodiscard]] constexpr auto crc32c(std::string_view const s) noexcept -> std::uint32_t {
  return crc<crc32c_spec>(s);
}

}  // namespace nexenne::algorithm
