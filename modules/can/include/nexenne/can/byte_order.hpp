#pragma once

/**
 * @file
 * @brief Byte order for a CAN signal: little-endian (Intel) or big-endian (Motorola).
 *
 * A CAN signal that spans more than one byte must say which byte holds the most
 * significant bits. The two orders are the little-endian form, traditionally
 * called Intel, and the big-endian form, traditionally called Motorola. Both use
 * the same DBC bit numbering (byte index times eight plus the bit position within
 * the byte, where bit position zero is the least significant bit of the byte);
 * they differ only in direction:
 *
 * - little_endian: the start bit is the signal's least significant bit, and the
 *   signal grows toward higher bit numbers.
 * - big_endian: the start bit is the signal's most significant bit, and the
 *   signal grows toward lower bit numbers, wrapping at each byte boundary (the
 *   sawtooth layout).
 *
 * The mechanics live in \c packing_plan.hpp; this header only names the
 * choice. Reference: the Vector DBC signal byte-order convention.
 */

#include <cstdint>

namespace nexenne::can {

/**
 * @brief The byte order a multi-byte CAN signal uses within the payload.
 */
enum class byte_order : std::uint8_t {
  little_endian,  ///< Intel order: the start bit is the signal's least significant bit.
  big_endian,     ///< Motorola order: the start bit is the signal's most significant bit.
};

}  // namespace nexenne::can
