#pragma once

/**
 * @file
 * @brief xxHash, a fast non-cryptographic hash by Yann Collet (32 or 64 bit).
 *
 * Provides one-shot \c xxhash (over a byte span or a string view) and a
 * streaming \c xxhash_ctx that accepts chunked input. The 32- and 64-bit
 * variants share their structure but use different primes, rotations, and
 * stripe sizes. The 32-bit variant is the right choice on MCUs (no 64-bit
 * multiplies); the 64-bit variant is faster on 64-bit hosts and produces
 * stronger output for hash tables and content addressing.
 *
 * Multi-byte words are read little-endian on every host, so the output matches
 * the published xxHash vectors regardless of native byte order.
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

namespace detail {

inline constexpr auto xxh32_prime1{std::uint32_t{0x9E3779B1u}};
inline constexpr auto xxh32_prime2{std::uint32_t{0x85EBCA77u}};
inline constexpr auto xxh32_prime3{std::uint32_t{0xC2B2AE3Du}};
inline constexpr auto xxh32_prime4{std::uint32_t{0x27D4EB2Fu}};
inline constexpr auto xxh32_prime5{std::uint32_t{0x165667B1u}};

[[nodiscard]] inline auto xxh32_read32(std::uint8_t const* const p) noexcept -> std::uint32_t {
  auto v{std::uint32_t{0}};
  std::memcpy(&v, p, sizeof(v));
  if constexpr (std::endian::native == std::endian::big) {
    v = std::byteswap(v);
  }
  return v;
}

[[nodiscard]] inline auto
xxh32_round(std::uint32_t acc, std::uint32_t const input) noexcept -> std::uint32_t {
  acc += input * xxh32_prime2;
  acc = std::rotl(acc, 13);
  acc *= xxh32_prime1;
  return acc;
}

[[nodiscard]] inline auto xxh32_impl(
  std::span<std::uint8_t const> const bytes, std::uint32_t const seed
) noexcept -> std::uint32_t {
  auto const len{bytes.size()};
  auto const* p{bytes.data()};
  auto const* const end{p + len};
  auto h{std::uint32_t{0}};

  if (len >= 16) {
    auto const* const limit{end - 16};
    auto v1{seed + xxh32_prime1 + xxh32_prime2};
    auto v2{seed + xxh32_prime2};
    auto v3{seed};
    auto v4{seed - xxh32_prime1};

    do {
      v1 = xxh32_round(v1, xxh32_read32(p));
      p += 4;
      v2 = xxh32_round(v2, xxh32_read32(p));
      p += 4;
      v3 = xxh32_round(v3, xxh32_read32(p));
      p += 4;
      v4 = xxh32_round(v4, xxh32_read32(p));
      p += 4;
    } while (p <= limit);

    h = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
  } else {
    h = seed + xxh32_prime5;
  }

  h += static_cast<std::uint32_t>(len);

  while (p + 4 <= end) {
    h += xxh32_read32(p) * xxh32_prime3;
    h = std::rotl(h, 17) * xxh32_prime4;
    p += 4;
  }
  while (p < end) {
    h += static_cast<std::uint32_t>(*p) * xxh32_prime5;
    h = std::rotl(h, 11) * xxh32_prime1;
    ++p;
  }

  h ^= h >> 15u;
  h *= xxh32_prime2;
  h ^= h >> 13u;
  h *= xxh32_prime3;
  h ^= h >> 16u;
  return h;
}

inline constexpr auto xxh64_prime1{std::uint64_t{0x9E3779B185EBCA87ULL}};
inline constexpr auto xxh64_prime2{std::uint64_t{0xC2B2AE3D27D4EB4FULL}};
inline constexpr auto xxh64_prime3{std::uint64_t{0x165667B19E3779F9ULL}};
inline constexpr auto xxh64_prime4{std::uint64_t{0x85EBCA77C2B2AE63ULL}};
inline constexpr auto xxh64_prime5{std::uint64_t{0x27D4EB2F165667C5ULL}};

[[nodiscard]] inline auto xxh64_read64(std::uint8_t const* const p) noexcept -> std::uint64_t {
  auto v{std::uint64_t{0}};
  std::memcpy(&v, p, sizeof(v));
  if constexpr (std::endian::native == std::endian::big) {
    v = std::byteswap(v);
  }
  return v;
}

[[nodiscard]] inline auto xxh64_read32(std::uint8_t const* const p) noexcept -> std::uint32_t {
  auto v{std::uint32_t{0}};
  std::memcpy(&v, p, sizeof(v));
  if constexpr (std::endian::native == std::endian::big) {
    v = std::byteswap(v);
  }
  return v;
}

[[nodiscard]] inline auto
xxh64_round(std::uint64_t acc, std::uint64_t const input) noexcept -> std::uint64_t {
  acc += input * xxh64_prime2;
  acc = std::rotl(acc, 31);
  acc *= xxh64_prime1;
  return acc;
}

[[nodiscard]] inline auto
xxh64_merge_round(std::uint64_t acc, std::uint64_t val) noexcept -> std::uint64_t {
  val = xxh64_round(0, val);
  acc ^= val;
  acc = acc * xxh64_prime1 + xxh64_prime4;
  return acc;
}

[[nodiscard]] inline auto xxh64_impl(
  std::span<std::uint8_t const> const bytes, std::uint64_t const seed
) noexcept -> std::uint64_t {
  auto const len{bytes.size()};
  auto const* p{bytes.data()};
  auto const* const end{p + len};
  auto h{std::uint64_t{0}};

  if (len >= 32) {
    auto const* const limit{end - 32};
    auto v1{seed + xxh64_prime1 + xxh64_prime2};
    auto v2{seed + xxh64_prime2};
    auto v3{seed};
    auto v4{seed - xxh64_prime1};

    do {
      v1 = xxh64_round(v1, xxh64_read64(p));
      p += 8;
      v2 = xxh64_round(v2, xxh64_read64(p));
      p += 8;
      v3 = xxh64_round(v3, xxh64_read64(p));
      p += 8;
      v4 = xxh64_round(v4, xxh64_read64(p));
      p += 8;
    } while (p <= limit);

    h = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
    h = xxh64_merge_round(h, v1);
    h = xxh64_merge_round(h, v2);
    h = xxh64_merge_round(h, v3);
    h = xxh64_merge_round(h, v4);
  } else {
    h = seed + xxh64_prime5;
  }

  h += static_cast<std::uint64_t>(len);

  while (p + 8 <= end) {
    auto const k1{xxh64_round(0, xxh64_read64(p))};
    h ^= k1;
    h = std::rotl(h, 27) * xxh64_prime1 + xxh64_prime4;
    p += 8;
  }
  if (p + 4 <= end) {
    h ^= static_cast<std::uint64_t>(xxh64_read32(p)) * xxh64_prime1;
    h = std::rotl(h, 23) * xxh64_prime2 + xxh64_prime3;
    p += 4;
  }
  while (p < end) {
    h ^= static_cast<std::uint64_t>(*p) * xxh64_prime5;
    h = std::rotl(h, 11) * xxh64_prime1;
    ++p;
  }

  h ^= h >> 33u;
  h *= xxh64_prime2;
  h ^= h >> 29u;
  h *= xxh64_prime3;
  h ^= h >> 32u;
  return h;
}

template <std::size_t Width>
struct xxhash_word;

template <>
struct xxhash_word<32> {
  using type = std::uint32_t;
};

template <>
struct xxhash_word<64> {
  using type = std::uint64_t;
};

}  // namespace detail

/// @brief The unsigned result type of \c xxhash at the given \c Width.
template <std::size_t Width>
using xxhash_result_t = typename detail::xxhash_word<Width>::type;

/**
 * @brief One-shot xxHash of a byte span.
 *
 * Dispatches to the 32- or 64-bit implementation per \p Width.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 * @param bytes Bytes to hash. An empty span hashes the length only.
 * @param seed Hash seed; defaults to zero.
 *
 * @return The Width-bit xxHash of \p bytes.
 *
 * @pre None.
 * @post Equal inputs and seeds always produce the same hash value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 *
 * @warning Not cryptographically secure.
 */
template <std::size_t Width = 64>
  requires(Width == 32 || Width == 64)
[[nodiscard]] inline auto xxhash(
  std::span<std::uint8_t const> const bytes, xxhash_result_t<Width> const seed = 0
) noexcept -> xxhash_result_t<Width> {
  if constexpr (Width == 32) {
    return detail::xxh32_impl(bytes, seed);
  } else {
    return detail::xxh64_impl(bytes, seed);
  }
}

/**
 * @brief One-shot xxHash of a string view.
 *
 * Reinterprets the characters of \p s as bytes and forwards to the byte-span
 * overload.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 * @param s Characters to hash. An empty view hashes the length only.
 * @param seed Hash seed; defaults to zero.
 *
 * @return The Width-bit xxHash of \p s.
 *
 * @pre None.
 * @post The result matches the byte-span overload over the same bytes.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 *
 * @warning Not cryptographically secure.
 */
template <std::size_t Width = 64>
  requires(Width == 32 || Width == 64)
[[nodiscard]] inline auto xxhash(
  std::string_view const s, xxhash_result_t<Width> const seed = 0
) noexcept -> xxhash_result_t<Width> {
  return xxhash<Width>(
    std::span<std::uint8_t const>{reinterpret_cast<std::uint8_t const*>(s.data()), s.size()}, seed
  );
}

/**
 * @brief Streaming xxHash context: feed bytes via \c update, finalize with
 *        \c value.
 *
 * Buffers a single internal stripe (16 bytes at width 32, 32 at width 64) so
 * chunked input works regardless of boundary alignment. The finalized result
 * matches the one-shot \c xxhash of the concatenated input.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 */
template <std::size_t Width>
  requires(Width == 32 || Width == 64)
class xxhash_ctx {
public:
  using value_type = xxhash_result_t<Width>;
  static constexpr std::size_t stripe_size{Width == 32 ? 16u : 32u};

private:
  value_type m_v1{};
  value_type m_v2{};
  value_type m_v3{};
  value_type m_v4{};
  value_type m_seed{0};
  std::uint64_t m_total{0};
  std::array<std::uint8_t, stripe_size> m_buf{};
  std::size_t m_buf_n{0};

  auto consume_stripe(std::uint8_t const* const p) noexcept -> void {
    if constexpr (Width == 32) {
      m_v1 = detail::xxh32_round(m_v1, detail::xxh32_read32(p + 0));
      m_v2 = detail::xxh32_round(m_v2, detail::xxh32_read32(p + 4));
      m_v3 = detail::xxh32_round(m_v3, detail::xxh32_read32(p + 8));
      m_v4 = detail::xxh32_round(m_v4, detail::xxh32_read32(p + 12));
    } else {
      m_v1 = detail::xxh64_round(m_v1, detail::xxh64_read64(p + 0));
      m_v2 = detail::xxh64_round(m_v2, detail::xxh64_read64(p + 8));
      m_v3 = detail::xxh64_round(m_v3, detail::xxh64_read64(p + 16));
      m_v4 = detail::xxh64_round(m_v4, detail::xxh64_read64(p + 24));
    }
  }

public:
  /**
   * @brief Constructs a streaming context seeded with \p seed.
   *
   * @param seed Hash seed; defaults to zero.
   *
   * @pre None.
   * @post The context holds no buffered bytes and \c value() equals the
   *       one-shot \c xxhash of the empty input under \p seed.
   */
  explicit xxhash_ctx(value_type const seed = 0) noexcept : m_seed{seed} {
    reset(seed);
  }

  /**
   * @brief Resets the context to a fresh stream seeded with \p seed.
   *
   * @param seed New hash seed; defaults to zero.
   *
   * @pre None.
   * @post The context holds no buffered bytes and behaves as if freshly
   *       constructed with \p seed.
   */
  auto reset(value_type const seed = 0) noexcept -> void {
    m_seed = seed;
    m_total = 0;
    m_buf_n = 0;
    if constexpr (Width == 32) {
      m_v1 = seed + detail::xxh32_prime1 + detail::xxh32_prime2;
      m_v2 = seed + detail::xxh32_prime2;
      m_v3 = seed;
      m_v4 = seed - detail::xxh32_prime1;
    } else {
      m_v1 = seed + detail::xxh64_prime1 + detail::xxh64_prime2;
      m_v2 = seed + detail::xxh64_prime2;
      m_v3 = seed;
      m_v4 = seed - detail::xxh64_prime1;
    }
  }

  /**
   * @brief Feeds a byte span into the streaming hash.
   *
   * Completes any partially buffered stripe, consumes whole stripes directly,
   * and buffers the trailing partial stripe for the next call, so input may
   * arrive in chunks of any size.
   *
   * @param data Bytes to append to the stream. An empty span is a no-op.
   *
   * @pre None.
   * @post The total length and accumulators reflect every byte fed since the
   *       last reset, in order.
   *
   * @complexity \c O(N) in the size \c N of \p data.
   */
  auto update(std::span<std::uint8_t const> const data) noexcept -> void {
    auto p{data.data()};
    auto n{data.size()};
    m_total += n;

    if (m_buf_n > 0) {
      auto const fill{stripe_size - m_buf_n};
      if (n < fill) {
        std::memcpy(m_buf.data() + m_buf_n, p, n);
        m_buf_n += n;
        return;
      }
      std::memcpy(m_buf.data() + m_buf_n, p, fill);
      consume_stripe(m_buf.data());
      p += fill;
      n -= fill;
      m_buf_n = 0;
    }
    while (n >= stripe_size) {
      consume_stripe(p);
      p += stripe_size;
      n -= stripe_size;
    }
    if (n > 0) {
      std::memcpy(m_buf.data(), p, n);
      m_buf_n = n;
    }
  }

  /**
   * @brief Feeds the characters of a string view into the streaming hash.
   *
   * @param s Characters to append to the stream. An empty view is a no-op.
   *
   * @pre None.
   * @post The accumulators reflect every character fed since the last reset.
   *
   * @complexity \c O(N) in the length \c N of \p s.
   */
  auto update(std::string_view const s) noexcept -> void {
    update(std::span<std::uint8_t const>{reinterpret_cast<std::uint8_t const*>(s.data()), s.size()}
    );
  }

  /**
   * @brief Finalizes and returns the xxHash of all bytes fed so far.
   *
   * Mixes the buffered trailing bytes and the accumulators into the digest
   * without modifying the context, so it may be called repeatedly.
   *
   * @return The Width-bit xxHash of the concatenated input.
   *
   * @pre None.
   * @post The context is unchanged; the result equals the one-shot \c xxhash
   *       of the concatenated input under the same seed.
   *
   * @complexity \c O(1) beyond the at-most-one buffered stripe.
   */
  [[nodiscard]] auto value() const noexcept -> value_type {
    if constexpr (Width == 32) {
      value_type h{};
      if (m_total >= 16) {
        h = std::rotl(m_v1, 1) + std::rotl(m_v2, 7) + std::rotl(m_v3, 12) + std::rotl(m_v4, 18);
      } else {
        h = m_seed + detail::xxh32_prime5;
      }
      h += static_cast<value_type>(m_total);
      auto const* p{m_buf.data()};
      auto const* const end{p + m_buf_n};
      while (p + 4 <= end) {
        h += detail::xxh32_read32(p) * detail::xxh32_prime3;
        h = std::rotl(h, 17) * detail::xxh32_prime4;
        p += 4;
      }
      while (p < end) {
        h += static_cast<value_type>(*p) * detail::xxh32_prime5;
        h = std::rotl(h, 11) * detail::xxh32_prime1;
        ++p;
      }
      h ^= h >> 15u;
      h *= detail::xxh32_prime2;
      h ^= h >> 13u;
      h *= detail::xxh32_prime3;
      h ^= h >> 16u;
      return h;
    } else {
      value_type h{};
      if (m_total >= 32) {
        h = std::rotl(m_v1, 1) + std::rotl(m_v2, 7) + std::rotl(m_v3, 12) + std::rotl(m_v4, 18);
        h = detail::xxh64_merge_round(h, m_v1);
        h = detail::xxh64_merge_round(h, m_v2);
        h = detail::xxh64_merge_round(h, m_v3);
        h = detail::xxh64_merge_round(h, m_v4);
      } else {
        h = m_seed + detail::xxh64_prime5;
      }
      h += static_cast<value_type>(m_total);
      auto const* p{m_buf.data()};
      auto const* const end{p + m_buf_n};
      while (p + 8 <= end) {
        auto const k1{detail::xxh64_round(0, detail::xxh64_read64(p))};
        h ^= k1;
        h = std::rotl(h, 27) * detail::xxh64_prime1 + detail::xxh64_prime4;
        p += 8;
      }
      if (p + 4 <= end) {
        h ^= static_cast<value_type>(detail::xxh64_read32(p)) * detail::xxh64_prime1;
        h = std::rotl(h, 23) * detail::xxh64_prime2 + detail::xxh64_prime3;
        p += 4;
      }
      while (p < end) {
        h ^= static_cast<value_type>(*p) * detail::xxh64_prime5;
        h = std::rotl(h, 11) * detail::xxh64_prime1;
        ++p;
      }
      h ^= h >> 33u;
      h *= detail::xxh64_prime2;
      h ^= h >> 29u;
      h *= detail::xxh64_prime3;
      h ^= h >> 32u;
      return h;
    }
  }
};

}  // namespace nexenne::algorithm
