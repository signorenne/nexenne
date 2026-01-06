#pragma once

/**
 * @file
 * @brief Bit-manipulation helpers that extend the standard library's bit ops.
 *
 * The standard library already provides \c std::popcount, \c std::countl_zero,
 * \c std::countr_zero, \c std::bit_width, \c std::bit_floor, \c std::bit_ceil,
 * \c std::has_single_bit, \c std::rotl, \c std::rotr, and \c std::bit_cast;
 * include the standard bit header for those. This header adds what it leaves
 * out: bit reversal, single-bit set/clear/toggle/test with debug bounds
 * checks, contiguous-mask building, bitfield pack/extract, and set-bit
 * iteration. Everything is \c constexpr over any \c std::unsigned_integral.
 */

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>

namespace nexenne::utility {

/**
 * @brief Reverses the bit order of \p x.
 *
 * Produces the value whose bit at position \c i equals the bit at position
 * \c width-1-i of \p x. For an 8-bit input, \c 0b1101'0000 becomes
 * \c 0b0000'1011.
 *
 * @tparam T Unsigned integer type.
 * @param x Value whose bits are reversed.
 *
 * @return \p x with its bit order reversed across the full width of \p T.
 *
 * @pre None.
 * @post The bit at position \c i of the result equals the bit at position
 *       \c W-1-i of \p x, where \c W is the bit width of \p T.
 *
 * @complexity \c O(log W) in the bit width \c W: a divide-and-conquer butterfly
 *             swap (halves, then quarters, down to adjacent bits) rather than a
 *             per-bit loop, staying generic over every unsigned width.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto reverse_bits(T x) noexcept -> T {
  constexpr std::size_t bits{sizeof(T) * 8};
  auto mask{static_cast<T>(~T{0})};
  for (std::size_t shift{bits >> 1}; shift > 0; shift >>= 1) {
    // mask selects the low half of every 2*shift-bit block; swap those halves
    // with their high counterparts.
    mask = static_cast<T>(mask ^ static_cast<T>(mask << shift));
    x = static_cast<T>(static_cast<T>((x & ~mask) >> shift) | static_cast<T>((x & mask) << shift));
  }
  return x;
}

/**
 * @brief Tests whether bit \p i of \p x is set.
 *
 * @tparam T Unsigned integer type.
 * @param x Value to inspect.
 * @param i Bit index, counted from the least-significant bit.
 *
 * @return \c true when bit \p i of \p x is one.
 *
 * @pre \p i is less than the bit width of \p T; a larger index asserts in
 *      debug and is undefined behaviour in release.
 * @post None.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto test_bit(T const x, std::size_t const i) noexcept -> bool {
  assert(i < sizeof(T) * 8 && "test_bit: bit index out of range");
  return ((x >> i) & T{1}) != T{0};
}

/**
 * @brief Returns \p x with bit \p i set to one.
 *
 * @tparam T Unsigned integer type.
 * @param x Source value.
 * @param i Bit index, counted from the least-significant bit.
 *
 * @return Copy of \p x with bit \p i forced to one; other bits unchanged.
 *
 * @pre \p i is less than the bit width of \p T; a larger index asserts in
 *      debug and is undefined behaviour in release.
 * @post \c test_bit(result, i) is \c true.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto set_bit(T const x, std::size_t const i) noexcept -> T {
  assert(i < sizeof(T) * 8 && "set_bit: bit index out of range");
  return static_cast<T>(x | (T{1} << i));
}

/**
 * @brief Returns \p x with bit \p i cleared to zero.
 *
 * @tparam T Unsigned integer type.
 * @param x Source value.
 * @param i Bit index, counted from the least-significant bit.
 *
 * @return Copy of \p x with bit \p i forced to zero; other bits unchanged.
 *
 * @pre \p i is less than the bit width of \p T; a larger index asserts in
 *      debug and is undefined behaviour in release.
 * @post \c test_bit(result, i) is \c false.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto clear_bit(T const x, std::size_t const i) noexcept -> T {
  assert(i < sizeof(T) * 8 && "clear_bit: bit index out of range");
  return static_cast<T>(x & ~(T{1} << i));
}

/**
 * @brief Returns \p x with bit \p i flipped.
 *
 * @tparam T Unsigned integer type.
 * @param x Source value.
 * @param i Bit index, counted from the least-significant bit.
 *
 * @return Copy of \p x with bit \p i inverted; other bits unchanged.
 *
 * @pre \p i is less than the bit width of \p T; a larger index asserts in
 *      debug and is undefined behaviour in release.
 * @post \c test_bit(result, i) equals \c !test_bit(x, i).
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto toggle_bit(T const x, std::size_t const i) noexcept -> T {
  assert(i < sizeof(T) * 8 && "toggle_bit: bit index out of range");
  return static_cast<T>(x ^ (T{1} << i));
}

/**
 * @brief Builds a mask with bits \p low through \p high (inclusive) set.
 *
 * @tparam T Unsigned integer type.
 * @param low Index of the lowest bit to set.
 * @param high Index of the highest bit to set.
 *
 * @return Mask with bits \p low through \p high set, others zero.
 *
 * @pre \p high is greater than or equal to \p low, and less than the bit width
 *      of \p T; violations assert in debug.
 * @post Bits in \c [low, high] are one; all others are zero.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto
set_bits_mask(std::size_t const low, std::size_t const high) noexcept -> T {
  assert(high >= low && "set_bits_mask: high is below low");
  assert(high < sizeof(T) * 8 && "set_bits_mask: high out of range");
  auto const count{high - low + 1};
  auto const ones{
    count >= sizeof(T) * 8 ? static_cast<T>(~T{0}) : static_cast<T>((T{1} << count) - 1)
  };
  return static_cast<T>(ones << low);
}

/**
 * @brief Invokes \p fn with the index of each set bit in \p x, lowest first.
 *
 * Walks the set bits in ascending position order, reading the lowest set bit
 * with \c std::countr_zero and clearing it, so the cost is proportional to the
 * number of set bits, not the type width.
 *
 * @tparam T Unsigned integer type.
 * @tparam F Callable invocable with one \c std::size_t argument.
 * @param x Value whose set bits are visited.
 * @param fn Callback invoked once per set bit with its index.
 *
 * @pre None.
 * @post \p fn has been invoked exactly \c std::popcount(x) times.
 *
 * @complexity \c O(P) where \c P is the number of set bits in \p x.
 */
template <std::unsigned_integral T, typename F>
  requires std::invocable<F&, std::size_t>
constexpr auto for_each_set_bit(T x, F&& fn) -> void {
  while (x != T{0}) {
    auto const i{static_cast<std::size_t>(std::countr_zero(x))};
    fn(i);
    x &= static_cast<T>(x - 1);  // clear the lowest set bit
  }
}

/**
 * @brief Writes the low \p width bits of \p src into the bitfield at \p offset
 *        in \p dest.
 *
 * Clears the \p width bits of \p dest starting at \p offset, then writes the
 * low \p width bits of \p src there. Bits of \p dest outside the field are
 * preserved.
 *
 * @tparam T Unsigned integer type.
 * @param dest Destination value receiving the field.
 * @param src Source value whose low \p width bits are written.
 * @param offset Bit index of the field's least-significant bit.
 * @param width Number of bits in the field.
 *
 * @return Copy of \p dest with the field at \p offset replaced.
 *
 * @pre \p width is at least one and \c offset+width is at most the bit width of
 *      \p T; violations assert in debug.
 * @post \c extract_bits(result, offset, width) equals the low \p width bits of
 *       \p src.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto pack_bits(
  T const dest, T const src, std::size_t const offset, std::size_t const width
) noexcept -> T {
  assert(width >= 1 && "pack_bits: width must be at least one");
  assert(offset + width <= sizeof(T) * 8 && "pack_bits: field exceeds type width");
  auto const mask{set_bits_mask<T>(offset, offset + width - 1)};
  auto const value{static_cast<T>((src << offset) & mask)};
  return static_cast<T>((dest & ~mask) | value);
}

/**
 * @brief Extracts \p width bits at \p offset from \p x, right-aligned.
 *
 * @tparam T Unsigned integer type.
 * @param x Value to read the field from.
 * @param offset Bit index of the field's least-significant bit.
 * @param width Number of bits in the field.
 *
 * @return The \p width bits at \p offset of \p x, shifted down to bit zero and
 *         zero-extended.
 *
 * @pre \p width is at least one and \c offset+width is at most the bit width of
 *      \p T; violations assert in debug.
 * @post Bits above position \c width-1 of the result are zero.
 */
template <std::unsigned_integral T>
[[nodiscard]] constexpr auto
extract_bits(T const x, std::size_t const offset, std::size_t const width) noexcept -> T {
  assert(width >= 1 && "extract_bits: width must be at least one");
  assert(offset + width <= sizeof(T) * 8 && "extract_bits: field exceeds type width");
  auto const mask{static_cast<T>(width >= sizeof(T) * 8 ? ~T{0} : (T{1} << width) - 1)};
  return static_cast<T>((x >> offset) & mask);
}

}  // namespace nexenne::utility
