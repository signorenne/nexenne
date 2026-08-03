#pragma once

/**
 * @file
 * @brief Data length code (DLC) conversions for Classic CAN and CAN FD.
 *
 * The four-bit DLC field on the wire does not equal the byte count once a frame
 * is CAN FD. Classic CAN carries 0 to 8 data bytes and a DLC of 9 through 15 is
 * clamped to 8. CAN FD reuses the same four bits to select one of sixteen
 * discrete lengths, 0 through 8 then 12, 16, 20, 24, 32, 48, and 64, so a DLC of
 * 12 means 24 bytes, not 12. These helpers own that mapping in both directions
 * and the rounding rule a transmitter needs: an FD payload whose length is not
 * one of the discrete sizes is padded up to the next one.
 *
 * Reference: ISO 11898-1:2015, the CAN FD DLC-to-length table.
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace nexenne::can {

/**
 * @brief The sixteen CAN FD data lengths indexed by DLC.
 *
 * Entry \c i is the byte count a CAN FD frame carries when its DLC field holds
 * \c i: identity for 0 through 8, then the discrete jumps 12, 16, 20, 24, 32,
 * 48, and 64.
 */
inline constexpr std::array<std::uint8_t, 16> fd_dlc_length_table{
  0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
};

/**
 * @brief The largest data length any CAN FD frame can carry.
 */
inline constexpr std::uint8_t max_fd_length{64};

/**
 * @brief The largest data length any Classic CAN frame can carry.
 */
inline constexpr std::uint8_t max_classic_length{8};

/**
 * @brief Reverse table: the smallest CAN FD DLC whose length covers each byte count.
 *
 * Entry \c i is the DLC whose \c fd_dlc_length_table length is the smallest one at
 * least \c i, for \c i in \c [0, 64]. Precomputed from the forward table at
 * compile time so the transmit path maps a length to a DLC with a single load
 * rather than a linear scan (see \c fd_length_to_dlc).
 */
inline constexpr std::array<std::uint8_t, max_fd_length + 1U> fd_length_dlc_table{[] {
  std::array<std::uint8_t, max_fd_length + 1U> table{};
  std::size_t dlc{0};
  for (std::size_t length{0}; length <= max_fd_length; ++length) {
    while (fd_dlc_length_table[dlc] < length) {
      ++dlc;
    }
    table[length] = static_cast<std::uint8_t>(dlc);
  }
  return table;
}()};

/**
 * @brief Byte count carried by a Classic CAN frame with the given DLC.
 *
 * @param dlc Four-bit data length code in the range \c [0, 15].
 *
 * @return \p dlc itself for 0 through 8, otherwise 8 (a DLC above 8 is clamped).
 *
 * @pre \p dlc is at most 15.
 * @post The result is at most 8.
 */
[[nodiscard]] constexpr auto classic_dlc_to_length(std::uint8_t const dlc
) noexcept -> std::uint8_t {
  return dlc < max_classic_length ? dlc : max_classic_length;
}

/**
 * @brief Byte count carried by a CAN FD frame with the given DLC.
 *
 * @param dlc Four-bit data length code in the range \c [0, 15].
 *
 * @return The table length for \p dlc, one of 0 through 8, 12, 16, 20, 24, 32,
 *         48, or 64.
 *
 * @pre \p dlc is at most 15; a larger value asserts in debug and is masked to
 *      its low four bits in release.
 * @post The result is at most 64.
 */
[[nodiscard]] constexpr auto fd_dlc_to_length(std::uint8_t const dlc) noexcept -> std::uint8_t {
  assert(dlc <= 15U && "fd_dlc_to_length: dlc exceeds four bits");
  return fd_dlc_length_table[dlc & 0x0FU];
}

/**
 * @brief Smallest Classic CAN DLC that carries at least \p length bytes.
 *
 * @param length Desired byte count in the range \c [0, 8].
 *
 * @return \p length for 0 through 8, clamped to 8 above that.
 *
 * @pre None.
 * @post The result is at most 8 and \c classic_dlc_to_length(result) is at
 *       least \c min(length, 8).
 */
[[nodiscard]] constexpr auto classic_length_to_dlc(std::uint8_t const length
) noexcept -> std::uint8_t {
  return length < max_classic_length ? length : max_classic_length;
}

/**
 * @brief Smallest CAN FD DLC whose length is at least \p length bytes.
 *
 * A length that is not one of the sixteen discrete sizes selects the next size
 * up, so a transmitter pads the payload rather than dropping bytes.
 *
 * @param length Desired byte count in the range \c [0, 64].
 *
 * @return The DLC in \c [0, 15] whose table length is the smallest one that is
 *         at least \p length; 15 (length 64) when \p length exceeds 64.
 *
 * @pre None.
 * @post \c fd_dlc_to_length(result) is at least \c min(length, 64).
 */
[[nodiscard]] constexpr auto fd_length_to_dlc(std::uint8_t const length) noexcept -> std::uint8_t {
  return length > max_fd_length ? std::uint8_t{15} : fd_length_dlc_table[length];
}

/**
 * @brief The on-wire CAN FD length for a payload of \p length bytes.
 *
 * Rounds \p length up to the next discrete CAN FD size, the length a transmitter
 * actually sends after zero padding.
 *
 * @param length Desired byte count in the range \c [0, 64].
 *
 * @return The smallest discrete CAN FD length that is at least \p length, capped
 *         at 64.
 *
 * @pre None.
 * @post The result is one of the sixteen CAN FD lengths and is at least
 *       \c min(length, 64).
 */
[[nodiscard]] constexpr auto fd_padded_length(std::uint8_t const length) noexcept -> std::uint8_t {
  return fd_dlc_to_length(fd_length_to_dlc(length));
}

/**
 * @brief Reports whether \p length is a valid Classic CAN data length.
 *
 * @param length Byte count to check.
 *
 * @return \c true when \p length is at most 8.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto is_valid_classic_length(std::uint8_t const length) noexcept -> bool {
  return length <= max_classic_length;
}

/**
 * @brief Reports whether \p length is one of the discrete CAN FD data lengths.
 *
 * @param length Byte count to check.
 *
 * @return \c true when \p length is 0 through 8 or 12, 16, 20, 24, 32, 48, 64.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto is_valid_fd_length(std::uint8_t const length) noexcept -> bool {
  return length <= max_fd_length && fd_padded_length(length) == length;
}

}  // namespace nexenne::can
