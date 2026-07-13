#pragma once

/**
 * @file
 * @brief MurmurHash3, a high-quality non-cryptographic hash.
 *
 * Provides \c murmur3 at width 32 (returning a \c std::uint32_t, the x86_32
 * reference variant) and width 128 (returning a pair of \c std::uint64_t, the
 * x64_128 variant), over a byte span or a string view. Both are noise-free on
 * short and long inputs and beat FNV on quality across the standard suites
 * (SMHasher). Use the 128-bit variant when you need a long fingerprint with
 * extremely low collision probability (deduplication, bloom-filter inputs).
 * When you do not need \c constexpr and have no specific reason otherwise, this
 * is the default of the hash family; \c xxhash is faster on large inputs.
 *
 * Multi-byte words are read little-endian on every host, so the output matches
 * the published MurmurHash3 vectors regardless of native byte order.
 *
 * @warning Not cryptographically secure.
 */

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace nexenne::algorithm {

/// @cond INTERNAL
namespace detail {

/**
 * @brief Reads four bytes at \p p as a little-endian \c std::uint32_t.
 *
 * Copies the bytes with \c std::memcpy (no aliasing violation) and byte-swaps
 * on a big-endian host, so the result matches the published MurmurHash3 vectors
 * regardless of native byte order.
 *
 * @param p Pointer to at least four readable bytes.
 *
 * @return The little-endian word at \p p.
 *
 * @pre \p p addresses at least four readable bytes.
 * @post None.
 */
[[nodiscard]] inline auto read_u32_le(std::uint8_t const* const p) noexcept -> std::uint32_t {
  auto v{std::uint32_t{0}};
  std::memcpy(&v, p, sizeof(v));
  if constexpr (std::endian::native == std::endian::big) {
    v = std::byteswap(v);
  }
  return v;
}

/**
 * @brief Reads eight bytes at \p p as a little-endian \c std::uint64_t.
 *
 * Copies the bytes with \c std::memcpy and byte-swaps on a big-endian host, so
 * the result matches the published MurmurHash3 vectors on any byte order.
 *
 * @param p Pointer to at least eight readable bytes.
 *
 * @return The little-endian word at \p p.
 *
 * @pre \p p addresses at least eight readable bytes.
 * @post None.
 */
[[nodiscard]] inline auto read_u64_le(std::uint8_t const* const p) noexcept -> std::uint64_t {
  auto v{std::uint64_t{0}};
  std::memcpy(&v, p, sizeof(v));
  if constexpr (std::endian::native == std::endian::big) {
    v = std::byteswap(v);
  }
  return v;
}

/**
 * @brief MurmurHash3 32-bit finalizer: avalanches the accumulated hash.
 *
 * Applies the standard xor-shift and multiply mixing rounds that spread each
 * input bit across the whole word, so the finished hash passes the avalanche
 * tests.
 *
 * @param h Accumulated hash to finalize.
 *
 * @return The avalanched 32-bit hash.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto fmix32(std::uint32_t h) noexcept -> std::uint32_t {
  h ^= h >> 16u;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13u;
  h *= 0xC2B2AE35u;
  h ^= h >> 16u;
  return h;
}

/**
 * @brief MurmurHash3 64-bit finalizer: avalanches an accumulated lane.
 *
 * Applies the standard xor-shift and multiply mixing rounds that spread each
 * input bit across the whole word.
 *
 * @param k Accumulated lane value to finalize.
 *
 * @return The avalanched 64-bit value.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto fmix64(std::uint64_t k) noexcept -> std::uint64_t {
  k ^= k >> 33u;
  k *= 0xFF51AFD7ED558CCDULL;
  k ^= k >> 33u;
  k *= 0xC4CEB9FE1A85EC53ULL;
  k ^= k >> 33u;
  return k;
}

/**
 * @brief MurmurHash3 x86_32 core over a byte span.
 *
 * Processes \p bytes in 4-byte blocks with the two constants and rotations of
 * the reference algorithm, folds the trailing 1 to 3 bytes, mixes in the length,
 * and finalizes with \c fmix32.
 *
 * @param bytes Bytes to hash.
 * @param seed Hash seed.
 *
 * @return The 32-bit MurmurHash3 of \p bytes.
 *
 * @pre None.
 * @post Equal inputs and seeds always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 */
[[nodiscard]] inline auto murmur3_32_impl(
  std::span<std::uint8_t const> const bytes, std::uint32_t const seed
) noexcept -> std::uint32_t {
  constexpr auto c1{std::uint32_t{0xCC9E2D51u}};
  constexpr auto c2{std::uint32_t{0x1B873593u}};

  auto h{seed};
  auto const n{bytes.size()};
  auto const blocks{n / 4};

  for (auto i{std::size_t{0}}; i < blocks; ++i) {
    auto k{read_u32_le(bytes.data() + i * 4)};
    k *= c1;
    k = std::rotl(k, 15);
    k *= c2;
    h ^= k;
    h = std::rotl(h, 13);
    h = h * 5u + 0xE6546B64u;
  }

  auto const* const tail{bytes.data() + blocks * 4};
  auto k1{std::uint32_t{0}};
  switch (n & 3u) {
    case 3:
      k1 ^= static_cast<std::uint32_t>(tail[2]) << 16u;
      [[fallthrough]];
    case 2:
      k1 ^= static_cast<std::uint32_t>(tail[1]) << 8u;
      [[fallthrough]];
    case 1:
      k1 ^= static_cast<std::uint32_t>(tail[0]);
      k1 *= c1;
      k1 = std::rotl(k1, 15);
      k1 *= c2;
      h ^= k1;
      [[fallthrough]];
    default:
      break;
  }

  h ^= static_cast<std::uint32_t>(n);
  return fmix32(h);
}

/**
 * @brief MurmurHash3 x64_128 core over a byte span.
 *
 * Processes \p bytes in 16-byte blocks across two 64-bit lanes with the
 * reference constants and rotations, folds the trailing 1 to 15 bytes, mixes in
 * the length, and cross-finalizes both lanes with \c fmix64.
 *
 * @param bytes Bytes to hash.
 * @param seed Hash seed applied to both lanes.
 *
 * @return The two 64-bit halves of the 128-bit MurmurHash3 of \p bytes.
 *
 * @pre None.
 * @post Equal inputs and seeds always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 */
[[nodiscard]] inline auto murmur3_128_impl(
  std::span<std::uint8_t const> const bytes, std::uint64_t const seed
) noexcept -> std::array<std::uint64_t, 2> {
  constexpr auto c1{std::uint64_t{0x87C37B91114253D5ULL}};
  constexpr auto c2{std::uint64_t{0x4CF5AD432745937FULL}};

  auto h1{seed};
  auto h2{seed};

  auto const n{bytes.size()};
  auto const nblocks{n / 16};

  for (auto i{std::size_t{0}}; i < nblocks; ++i) {
    auto k1{read_u64_le(bytes.data() + i * 16)};
    auto k2{read_u64_le(bytes.data() + i * 16 + 8)};

    k1 *= c1;
    k1 = std::rotl(k1, 31);
    k1 *= c2;
    h1 ^= k1;
    h1 = std::rotl(h1, 27);
    h1 += h2;
    h1 = h1 * 5u + 0x52DCE729u;

    k2 *= c2;
    k2 = std::rotl(k2, 33);
    k2 *= c1;
    h2 ^= k2;
    h2 = std::rotl(h2, 31);
    h2 += h1;
    h2 = h2 * 5u + 0x38495AB5u;
  }

  auto const* const tail{bytes.data() + nblocks * 16};
  auto k1{std::uint64_t{0}};
  auto k2{std::uint64_t{0}};

  switch (n & 15u) {
    case 15:
      k2 ^= static_cast<std::uint64_t>(tail[14]) << 48u;
      [[fallthrough]];
    case 14:
      k2 ^= static_cast<std::uint64_t>(tail[13]) << 40u;
      [[fallthrough]];
    case 13:
      k2 ^= static_cast<std::uint64_t>(tail[12]) << 32u;
      [[fallthrough]];
    case 12:
      k2 ^= static_cast<std::uint64_t>(tail[11]) << 24u;
      [[fallthrough]];
    case 11:
      k2 ^= static_cast<std::uint64_t>(tail[10]) << 16u;
      [[fallthrough]];
    case 10:
      k2 ^= static_cast<std::uint64_t>(tail[9]) << 8u;
      [[fallthrough]];
    case 9:
      k2 ^= static_cast<std::uint64_t>(tail[8]);
      k2 *= c2;
      k2 = std::rotl(k2, 33);
      k2 *= c1;
      h2 ^= k2;
      [[fallthrough]];
    case 8:
      k1 ^= static_cast<std::uint64_t>(tail[7]) << 56u;
      [[fallthrough]];
    case 7:
      k1 ^= static_cast<std::uint64_t>(tail[6]) << 48u;
      [[fallthrough]];
    case 6:
      k1 ^= static_cast<std::uint64_t>(tail[5]) << 40u;
      [[fallthrough]];
    case 5:
      k1 ^= static_cast<std::uint64_t>(tail[4]) << 32u;
      [[fallthrough]];
    case 4:
      k1 ^= static_cast<std::uint64_t>(tail[3]) << 24u;
      [[fallthrough]];
    case 3:
      k1 ^= static_cast<std::uint64_t>(tail[2]) << 16u;
      [[fallthrough]];
    case 2:
      k1 ^= static_cast<std::uint64_t>(tail[1]) << 8u;
      [[fallthrough]];
    case 1:
      k1 ^= static_cast<std::uint64_t>(tail[0]);
      k1 *= c1;
      k1 = std::rotl(k1, 31);
      k1 *= c2;
      h1 ^= k1;
      [[fallthrough]];
    default:
      break;
  }

  h1 ^= static_cast<std::uint64_t>(n);
  h2 ^= static_cast<std::uint64_t>(n);
  h1 += h2;
  h2 += h1;
  h1 = fmix64(h1);
  h2 = fmix64(h2);
  h1 += h2;
  h2 += h1;

  return {h1, h2};
}

template <std::size_t Width>
struct murmur3_traits;

template <>
struct murmur3_traits<32> {
  using value_type = std::uint32_t;
  using seed_type = std::uint32_t;
};

template <>
struct murmur3_traits<128> {
  using value_type = std::array<std::uint64_t, 2>;
  using seed_type = std::uint64_t;
};

}  // namespace detail
/// @endcond

/// @brief The result type of \c murmur3 at the given \c Width.
template <std::size_t Width>
using murmur3_result_t = typename detail::murmur3_traits<Width>::value_type;

/// @brief The seed type of \c murmur3 at the given \c Width.
template <std::size_t Width>
using murmur3_seed_t = typename detail::murmur3_traits<Width>::seed_type;

/**
 * @brief MurmurHash3 hash of a byte span.
 *
 * Dispatches to the x86_32 variant for \p Width 32, returning a
 * \c std::uint32_t, and to the x64_128 variant for \p Width 128, returning a
 * pair of \c std::uint64_t.
 *
 * @tparam Width Hash width in bits, either 32 or 128.
 * @param bytes Bytes to hash. An empty span hashes the length only.
 * @param seed Hash seed; defaults to zero.
 *
 * @return The MurmurHash3 hash of \p bytes at the selected width.
 *
 * @pre None.
 * @post Equal inputs and seeds always produce the same hash value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 *
 * @warning Not cryptographically secure.
 */
template <std::size_t Width = 128>
  requires(Width == 32 || Width == 128)
[[nodiscard]] inline auto murmur3(
  std::span<std::uint8_t const> const bytes, murmur3_seed_t<Width> const seed = 0
) noexcept -> murmur3_result_t<Width> {
  if constexpr (Width == 32) {
    return detail::murmur3_32_impl(bytes, seed);
  } else {
    return detail::murmur3_128_impl(bytes, seed);
  }
}

/**
 * @brief MurmurHash3 hash of a string view.
 *
 * Reinterprets the characters of \p s as bytes and forwards to the byte-span
 * overload.
 *
 * @tparam Width Hash width in bits, either 32 or 128.
 * @param s Characters to hash. An empty view hashes the length only.
 * @param seed Hash seed; defaults to zero.
 *
 * @return The MurmurHash3 hash of \p s at the selected width.
 *
 * @pre None.
 * @post The result matches the byte-span overload over the same bytes.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 *
 * @warning Not cryptographically secure.
 */
template <std::size_t Width = 128>
  requires(Width == 32 || Width == 128)
[[nodiscard]] inline auto murmur3(
  std::string_view const s, murmur3_seed_t<Width> const seed = 0
) noexcept -> murmur3_result_t<Width> {
  return murmur3<Width>(
    std::span<std::uint8_t const>{reinterpret_cast<std::uint8_t const*>(s.data()), s.size()}, seed
  );
}

}  // namespace nexenne::algorithm
