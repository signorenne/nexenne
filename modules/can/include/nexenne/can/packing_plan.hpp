#pragma once

/**
 * @file
 * @brief The compiled bit layout of a CAN signal as a list of byte chunks.
 *
 * Reading a signal out of a frame means gathering some bits from one or more
 * payload bytes and assembling them into an integer. The bit geometry (which
 * bytes, which bit positions, in which order) depends only on the signal's start
 * bit, width, and byte order, never on the frame, so it is computed once into a
 * \c packing_plan and reused for every frame.
 *
 * The key simplification: both Intel and Motorola layouts reduce to the same
 * per-byte operation once the chunks are computed. A chunk says "take \c num_bits
 * starting at bit \c lsb_in_byte of byte \c byte, and they are bits
 * \c [dest_shift, dest_shift + num_bits) of the value". Extraction is then
 * \c value |= ((payload[byte] >> lsb_in_byte) & mask) << dest_shift for each
 * chunk, and insertion is the inverse. Only the chunk computation differs between
 * the two byte orders; the gather and scatter loops are identical.
 *
 * Reference: the Vector DBC signal byte-order convention (see
 * \c byte_order.hpp).
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/signal.hpp>

namespace nexenne::can {

/**
 * @brief One contiguous run of bits taken from a single payload byte.
 *
 * The fields are everything a gather or scatter step needs: which byte, where in
 * it, how many bits, where they sit in the assembled value, and a precomputed
 * width mask so the hot path never rebuilds it.
 */
struct plan_chunk {
  using value_type = std::uint8_t;  ///< The element type of the chunk's bit counts.

  std::uint16_t byte{0};        ///< Payload byte index the bits come from.
  std::uint8_t lsb_in_byte{0};  ///< Lowest bit position within the byte (0 = LSB).
  std::uint8_t num_bits{0};     ///< Number of bits taken from this byte.
  std::uint8_t dest_shift{0};   ///< Position of this run's lowest bit in the value.
  std::uint8_t mask{0};         ///< Precomputed width mask (\c (1<<num_bits)-1), set at build time.
};

/**
 * @brief The compiled bit layout of a signal: its byte chunks and signedness.
 *
 * Build one with \c from_signal (or the explicit constructor), then call
 * \c extract / \c insert to move the raw integer in and out of a payload
 * span. The plan holds at most nine chunks, which is enough for a 64-bit field
 * straddling nine bytes; it is trivially copyable and never allocates.
 */
class packing_plan {
public:
  using value_type = std::uint64_t;

  /// @brief Largest number of byte chunks a 64-bit field can produce.
  static constexpr std::size_t max_chunks{9};

private:
  std::array<plan_chunk, max_chunks> m_chunks{};
  std::uint8_t m_count{0};
  std::uint8_t m_bit_length{1};
  bool m_is_signed{false};
  std::uint16_t m_required_length{0};

  constexpr auto push_chunk(plan_chunk chunk) noexcept -> void {
    // Precompute the width mask once so extract / insert avoid rebuilding it per
    // chunk on every frame at bus rate. A chunk lies within one byte, so
    // num_bits is at most 8 and the mask fits a byte.
    chunk.mask = static_cast<std::uint8_t>((1U << chunk.num_bits) - 1U);
    m_chunks[m_count] = chunk;
    ++m_count;
    auto const reach{static_cast<std::uint16_t>(chunk.byte + 1)};
    if (reach > m_required_length) {
      m_required_length = reach;
    }
  }

  // Little-endian (Intel): the start bit is the value's LSB and the field grows toward higher
  // DBC bit numbers, so the bits are contiguous. Walk byte by byte, taking as
  // many bits as the current byte still has above the running offset.
  constexpr auto build_little_endian(std::uint16_t const start_bit) noexcept -> void {
    auto remaining{m_bit_length};
    // Advance the bit cursor in a wider type so a field near the 16-bit start-bit
    // limit cannot wrap and alias byte 0.
    std::uint32_t global{start_bit};
    std::uint8_t dest{0};
    while (remaining != 0) {
      auto const byte{static_cast<std::uint16_t>(global / 8U)};
      auto const lsb{static_cast<std::uint8_t>(global % 8U)};
      auto const take{static_cast<std::uint8_t>(std::min<std::uint32_t>(8U - lsb, remaining))};
      push_chunk(plan_chunk{byte, lsb, take, dest});
      dest = static_cast<std::uint8_t>(dest + take);
      global = global + take;
      remaining = static_cast<std::uint8_t>(remaining - take);
    }
  }

  // Big-endian (Motorola): the start bit is the value's MSB. Moving to less significant bits
  // decreases the bit position within a byte and, at a byte boundary, jumps to
  // bit 7 of the next byte (the sawtooth). Each byte contributes a contiguous
  // run whose lowest bit maps to value position remaining - take.
  constexpr auto build_big_endian(std::uint16_t const start_bit) noexcept -> void {
    auto remaining{m_bit_length};
    auto byte{static_cast<std::uint16_t>(start_bit / 8U)};
    auto msb{static_cast<std::uint8_t>(start_bit % 8U)};  // bit position of the MSB in this byte
    while (remaining != 0) {
      auto const take{static_cast<std::uint8_t>(std::min<std::uint16_t>(msb + 1U, remaining))};
      auto const lsb{static_cast<std::uint8_t>(msb + 1U - take)};
      auto const dest{static_cast<std::uint8_t>(remaining - take)};
      push_chunk(plan_chunk{byte, lsb, take, dest});
      remaining = static_cast<std::uint8_t>(remaining - take);
      byte = static_cast<std::uint16_t>(byte + 1U);
      msb = 7U;  // every following byte starts its run at bit 7
    }
  }

public:
  /**
   * @brief Builds a plan from a start bit, width, byte order, and signedness.
   *
   * @param start_bit Start bit in DBC numbering.
   * @param bit_length Field width in bits, 1 through 64.
   * @param order Byte order of the field.
   * @param is_signed Whether the raw value is two's-complement signed.
   *
   * @pre \p bit_length is between 1 and 64.
   * @post \c bit_length() and \c is_signed() reflect the arguments and
   *       \c required_length() is the number of payload bytes the field touches.
   */
  constexpr packing_plan(
    std::uint16_t const start_bit,
    std::uint16_t const bit_length,
    byte_order const order,
    bool const is_signed
  ) noexcept
      : m_bit_length{static_cast<std::uint8_t>(bit_length)}, m_is_signed{is_signed} {
    assert(bit_length >= 1 && bit_length <= 64 && "packing_plan: bit_length out of range");
    if (order == byte_order::little_endian) {
      build_little_endian(start_bit);
    } else {
      build_big_endian(start_bit);
    }
  }

  /**
   * @brief Constructs the plan for a single unsigned little-endian bit at bit 0.
   *
   * @pre None.
   * @post \c bit_length() is one and the plan reads bit 0 of byte 0.
   */
  constexpr packing_plan() noexcept : packing_plan{0, 1, byte_order::little_endian, false} {}

  /**
   * @brief Compiles a \c signal into its packing plan.
   *
   * @param sig Signal whose bit layout to compile.
   *
   * @return The plan for \p sig.
   *
   * @pre \c sig.length() is between 1 and 64.
   * @post The plan reflects the signal's start bit, width, byte order, and sign.
   */
  [[nodiscard]] static constexpr auto from_signal(signal const& sig) noexcept -> packing_plan {
    return packing_plan{sig.start_bit(), sig.length(), sig.order(), sig.is_signed()};
  }

  /**
   * @brief The field width in bits.
   *
   * @return The stored bit length.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto bit_length() const noexcept -> std::uint8_t {
    return m_bit_length;
  }

  /**
   * @brief Whether the field is two's-complement signed.
   *
   * @return \c true when signed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_signed() const noexcept -> bool {
    return m_is_signed;
  }

  /**
   * @brief The number of byte chunks in the plan.
   *
   * @return The chunk count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto chunk_count() const noexcept -> std::uint8_t {
    return m_count;
  }

  /**
   * @brief Read-only view of the plan's chunks.
   *
   * @return A span of exactly \c chunk_count() chunks.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto chunks() const noexcept -> std::span<plan_chunk const> {
    return std::span<plan_chunk const>{m_chunks.data(), m_count};
  }

  /**
   * @brief The number of payload bytes the field touches.
   *
   * @return One past the highest byte index any chunk reads, so a payload of at
   *         least this many bytes holds the whole field.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto required_length() const noexcept -> std::uint16_t {
    return m_required_length;
  }

  /**
   * @brief Assembles the raw unsigned value from a payload span.
   *
   * Gathers each chunk and ORs it into place. The result is the field's bits,
   * right-aligned and zero-extended; sign interpretation is the caller's job.
   *
   * @param payload Payload bytes to read from.
   *
   * @return The raw value, with bits above \c bit_length() zero.
   *
   * @pre \c payload.size() is at least \c required_length(); a shorter span
   *      asserts in debug and is undefined in release.
   * @post Bits at and above position \c bit_length() of the result are zero.
   */
  [[nodiscard]] constexpr auto extract(std::span<std::byte const> const payload) const noexcept
    -> value_type {
    assert(payload.size() >= m_required_length && "packing_plan::extract: payload too short");
    value_type value{0};
    for (std::uint8_t i{0}; i < m_count; ++i) {
      plan_chunk const chunk{m_chunks[i]};
      auto const source{std::to_integer<value_type>(payload[chunk.byte])};
      auto const bits{(source >> chunk.lsb_in_byte) & value_type{chunk.mask}};
      value |= bits << chunk.dest_shift;
    }
    return value;
  }

  /**
   * @brief Writes the raw unsigned value into a payload span.
   *
   * Scatters each chunk back to its byte, clearing the field's bits first so the
   * surrounding bits of the payload are preserved.
   *
   * @param payload Payload bytes to write into.
   * @param raw Raw value whose low \c bit_length() bits are written.
   *
   * @pre \c payload.size() is at least \c required_length(); a shorter span
   *      asserts in debug and is undefined in release.
   * @post The field bits of \p payload hold the low \c bit_length() bits of
   *       \p raw; other bits are unchanged.
   */
  constexpr auto insert(std::span<std::byte> const payload, value_type const raw) const noexcept
    -> void {
    assert(payload.size() >= m_required_length && "packing_plan::insert: payload too short");
    for (std::uint8_t i{0}; i < m_count; ++i) {
      plan_chunk const chunk{m_chunks[i]};
      auto const piece{static_cast<unsigned>(raw >> chunk.dest_shift) & unsigned{chunk.mask}};
      auto const clear{~(unsigned{chunk.mask} << chunk.lsb_in_byte)};
      auto const byte{std::to_integer<unsigned>(payload[chunk.byte]) & clear};
      auto const updated{byte | (piece << chunk.lsb_in_byte)};
      payload[chunk.byte] = static_cast<std::byte>(static_cast<unsigned char>(updated));
    }
  }
};

static_assert(
  std::is_trivially_copyable_v<packing_plan>,
  "packing_plan must stay trivially copyable: it is a fixed array of plain chunks"
);

}  // namespace nexenne::can
