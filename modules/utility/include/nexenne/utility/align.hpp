#pragma once

/**
 * @file
 * @brief Power-of-two alignment helpers for sizes, offsets, and addresses.
 *
 * Rounding a size or address to a power-of-two boundary is the bread and butter
 * of bare-metal allocators (DMA buffers on a cache line, MMIO tables on a
 * natural boundary, a bump allocator advancing its cursor). This header offers
 * \c align_up, \c align_down, and \c is_aligned in an integral flavour (for
 * sizes and offsets) and a pointer flavour (for addresses). Every alignment
 * must be a non-zero power of two, asserted in debug; the mask trick the
 * formulas rely on is only correct for power-of-two alignments. The integral
 * overloads are \c constexpr; the pointer overloads are not, because they
 * round-trip through \c std::uintptr_t with \c reinterpret_cast, which a
 * constant expression forbids.
 */

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace nexenne::utility {

/**
 * @brief Rounds \p value up to the nearest multiple of \p alignment.
 *
 * Computes \c (value + (alignment - 1)) & ~(alignment - 1); a value already a
 * multiple of \p alignment is returned unchanged.
 *
 * @tparam Int Unsigned integer type of the value and alignment.
 * @param value Value (size or offset) to round up.
 * @param alignment Alignment boundary; must be a non-zero power of two.
 *
 * @return The smallest multiple of \p alignment at least \p value.
 *
 * @pre \p alignment is a non-zero power of two (asserted in debug).
 * @pre \c value+(alignment-1) does not overflow \p Int.
 * @post The result is a multiple of \p alignment and at least \p value.
 *
 * @complexity \c O(1).
 */
template <std::unsigned_integral Int>
[[nodiscard]] constexpr auto align_up(Int const value, Int const alignment) noexcept -> Int {
  assert(
    alignment != 0 && std::has_single_bit(alignment) && "align: alignment must be a power of two"
  );
  return static_cast<Int>((value + (alignment - 1)) & ~(alignment - 1));
}

/**
 * @brief Rounds \p value down to the nearest multiple of \p alignment.
 *
 * Computes \c value & ~(alignment - 1); a value already a multiple of
 * \p alignment is returned unchanged.
 *
 * @tparam Int Unsigned integer type of the value and alignment.
 * @param value Value (size or offset) to round down.
 * @param alignment Alignment boundary; must be a non-zero power of two.
 *
 * @return The largest multiple of \p alignment at most \p value.
 *
 * @pre \p alignment is a non-zero power of two (asserted in debug).
 * @post The result is a multiple of \p alignment and at most \p value.
 *
 * @complexity \c O(1).
 */
template <std::unsigned_integral Int>
[[nodiscard]] constexpr auto align_down(Int const value, Int const alignment) noexcept -> Int {
  assert(
    alignment != 0 && std::has_single_bit(alignment) && "align: alignment must be a power of two"
  );
  return static_cast<Int>(value & ~(alignment - 1));
}

/**
 * @brief Reports whether \p value is a multiple of \p alignment.
 *
 * @tparam Int Unsigned integer type of the value and alignment.
 * @param value Value (size or offset) to test.
 * @param alignment Alignment boundary; must be a non-zero power of two.
 *
 * @return \c true when \p value is a multiple of \p alignment.
 *
 * @pre \p alignment is a non-zero power of two (asserted in debug).
 * @post None.
 *
 * @complexity \c O(1).
 */
template <std::unsigned_integral Int>
[[nodiscard]] constexpr auto is_aligned(Int const value, Int const alignment) noexcept -> bool {
  assert(
    alignment != 0 && std::has_single_bit(alignment) && "align: alignment must be a power of two"
  );
  return (value & (alignment - 1)) == Int{0};
}

/**
 * @brief Rounds the address in \p ptr up to the next \p alignment boundary.
 *
 * Converts \p ptr to \c std::uintptr_t, rounds up with the integral overload,
 * and converts back. Works for \c T = \c void.
 *
 * @tparam T Pointee type; may be \c void.
 * @param ptr Pointer whose address is rounded up.
 * @param alignment Alignment boundary in bytes; a non-zero power of two.
 *
 * @return A pointer to the smallest \p alignment-aligned address at least the
 *         address of \p ptr.
 *
 * @pre \p alignment is a non-zero power of two (asserted in debug).
 * @pre The rounded-up address does not overflow \c std::uintptr_t.
 * @post \c is_aligned on the result with the same \p alignment is \c true.
 *
 * @complexity \c O(1).
 */
template <typename T>
[[nodiscard]] auto align_up(T* const ptr, std::size_t const alignment) noexcept -> T* {
  auto const address{reinterpret_cast<std::uintptr_t>(ptr)};
  return reinterpret_cast<T*>(align_up(address, static_cast<std::uintptr_t>(alignment)));
}

/**
 * @brief Rounds the address in \p ptr down to the prior \p alignment boundary.
 *
 * Converts \p ptr to \c std::uintptr_t, rounds down with the integral overload,
 * and converts back. Works for \c T = \c void.
 *
 * @tparam T Pointee type; may be \c void.
 * @param ptr Pointer whose address is rounded down.
 * @param alignment Alignment boundary in bytes; a non-zero power of two.
 *
 * @return A pointer to the largest \p alignment-aligned address at most the
 *         address of \p ptr.
 *
 * @pre \p alignment is a non-zero power of two (asserted in debug).
 * @post \c is_aligned on the result with the same \p alignment is \c true.
 *
 * @complexity \c O(1).
 */
template <typename T>
[[nodiscard]] auto align_down(T* const ptr, std::size_t const alignment) noexcept -> T* {
  auto const address{reinterpret_cast<std::uintptr_t>(ptr)};
  return reinterpret_cast<T*>(align_down(address, static_cast<std::uintptr_t>(alignment)));
}

/**
 * @brief Reports whether the address in \p ptr is \p alignment aligned.
 *
 * @tparam T Pointee type; may be \c void.
 * @param ptr Pointer whose address is tested.
 * @param alignment Alignment boundary in bytes; a non-zero power of two.
 *
 * @return \c true when the address of \p ptr is a multiple of \p alignment.
 *
 * @pre \p alignment is a non-zero power of two (asserted in debug).
 * @post None.
 *
 * @complexity \c O(1).
 */
template <typename T>
[[nodiscard]] auto is_aligned(T const* const ptr, std::size_t const alignment) noexcept -> bool {
  auto const address{reinterpret_cast<std::uintptr_t>(ptr)};
  return is_aligned(address, static_cast<std::uintptr_t>(alignment));
}

}  // namespace nexenne::utility
