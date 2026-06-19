#pragma once

/**
 * @file
 * @brief FNV-1a non-cryptographic hash, width-templated (32 or 64 bit).
 *
 * Provides one-shot \c fnv1a (over a byte span or a string view) and a
 * streaming \c fnv1a_ctx. Tiny: the inner loop is one XOR plus one multiply.
 * Quality is fine for hash tables on short keys, but worse than MurmurHash3 or
 * xxHash on larger inputs and worse than every good hash on adversarial inputs.
 * Reach for FNV when you need a \c constexpr hash (compile-time string or state
 * identifiers, dispatch tables), the keys are short, or code size on an MCU
 * matters more than collision-tail behaviour; otherwise prefer \c xxhash (fast,
 * high quality) or \c murmur3 (high quality, with a 128-bit variant).
 *
 * @warning Not cryptographically secure.
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace nexenne::algorithm {

namespace detail {

template <std::size_t Width>
struct fnv1a_params;

template <>
struct fnv1a_params<32> {
  using value_type = std::uint32_t;
  static constexpr value_type offset{0x811C9DC5u};
  static constexpr value_type prime{0x01000193u};
};

template <>
struct fnv1a_params<64> {
  using value_type = std::uint64_t;
  static constexpr value_type offset{0xCBF29CE484222325ULL};
  static constexpr value_type prime{0x00000100000001B3ULL};
};

}  // namespace detail

/// @brief The unsigned result type of \c fnv1a at the given \c Width.
template <std::size_t Width>
using fnv1a_result_t = typename detail::fnv1a_params<Width>::value_type;

/// @brief The FNV offset basis (default seed) for the given \c Width.
template <std::size_t Width>
inline constexpr auto fnv1a_offset{detail::fnv1a_params<Width>::offset};

/// @brief The FNV prime for the given \c Width.
template <std::size_t Width>
inline constexpr auto fnv1a_prime{detail::fnv1a_params<Width>::prime};

/**
 * @brief One-shot FNV-1a hash of a byte span.
 *
 * Folds each byte into the running hash with one XOR and one multiply by the
 * FNV prime, starting from \p seed. Fully \c constexpr, so it can produce
 * compile-time string and state identifiers.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 * @param bytes Bytes to hash. An empty span yields \p seed unchanged.
 * @param seed Initial hash value; defaults to the FNV offset basis.
 *
 * @return The Width-bit FNV-1a hash of \p bytes.
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
[[nodiscard]] constexpr auto
fnv1a(std::span<std::uint8_t const> const bytes, fnv1a_result_t<Width> const seed = fnv1a_offset<Width>) noexcept
  -> fnv1a_result_t<Width> {
  auto h{seed};
  for (auto const b : bytes) {
    h ^= static_cast<fnv1a_result_t<Width>>(b);
    h *= fnv1a_prime<Width>;
  }
  return h;
}

/**
 * @brief One-shot FNV-1a hash of a string view.
 *
 * Reinterprets each character of \p s as an unsigned byte and folds it into the
 * running hash, identically to the byte-span overload.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 * @param s Characters to hash. An empty view yields \p seed unchanged.
 * @param seed Initial hash value; defaults to the FNV offset basis.
 *
 * @return The Width-bit FNV-1a hash of \p s.
 *
 * @pre None.
 * @post Equal inputs and seeds always produce the same hash value, and the
 *       result matches the byte-span overload over the same bytes.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 *
 * @warning Not cryptographically secure.
 */
template <std::size_t Width = 64>
  requires(Width == 32 || Width == 64)
[[nodiscard]] constexpr auto
fnv1a(std::string_view const s, fnv1a_result_t<Width> const seed = fnv1a_offset<Width>) noexcept
  -> fnv1a_result_t<Width> {
  auto h{seed};
  for (auto const c : s) {
    h ^= static_cast<fnv1a_result_t<Width>>(static_cast<std::uint8_t>(c));
    h *= fnv1a_prime<Width>;
  }
  return h;
}

/**
 * @brief Streaming FNV-1a context, foldable byte by byte with no buffer.
 *
 * @tparam Width Hash width in bits, either 32 or 64.
 *
 * @pre None.
 * @post A default-constructed context holds the FNV offset basis.
 */
template <std::size_t Width>
  requires(Width == 32 || Width == 64)
class fnv1a_ctx {
public:
  using value_type = fnv1a_result_t<Width>;

private:
  value_type m_h{fnv1a_offset<Width>};

public:
  /**
   * @brief Constructs a context seeded with the FNV offset basis.
   *
   * @pre None.
   * @post \c value() equals the FNV offset basis for \c Width.
   */
  constexpr fnv1a_ctx() noexcept = default;

  /**
   * @brief Constructs a context seeded with \p seed.
   *
   * @param seed Initial hash state.
   *
   * @pre None.
   * @post \c value() equals \p seed.
   */
  explicit constexpr fnv1a_ctx(value_type const seed) noexcept : m_h{seed} {}

  /**
   * @brief Resets the running hash to \p seed, discarding all bytes fed.
   *
   * @param seed New initial hash state; defaults to the FNV offset basis.
   *
   * @pre None.
   * @post \c value() equals \p seed.
   */
  constexpr auto reset(value_type const seed = fnv1a_offset<Width>) noexcept -> void {
    m_h = seed;
  }

  /**
   * @brief Folds a byte span into the running hash.
   *
   * @param data Bytes to append to the stream. An empty span is a no-op.
   *
   * @pre None.
   * @post \c value() reflects every byte fed since the last reset, in order.
   *
   * @complexity \c O(N) in the size \c N of \p data.
   */
  constexpr auto update(std::span<std::uint8_t const> const data) noexcept -> void {
    for (auto const b : data) {
      m_h ^= static_cast<value_type>(b);
      m_h *= fnv1a_prime<Width>;
    }
  }

  /**
   * @brief Folds the characters of a string view into the running hash.
   *
   * @param s Characters to append to the stream. An empty view is a no-op.
   *
   * @pre None.
   * @post \c value() reflects every character fed since the last reset.
   *
   * @complexity \c O(N) in the length \c N of \p s.
   */
  constexpr auto update(std::string_view const s) noexcept -> void {
    for (auto const c : s) {
      m_h ^= static_cast<value_type>(static_cast<std::uint8_t>(c));
      m_h *= fnv1a_prime<Width>;
    }
  }

  /**
   * @brief Returns the current FNV-1a hash of all bytes fed so far.
   *
   * @return The running hash value.
   *
   * @pre None.
   * @post The context is unchanged; the result equals the one-shot \c fnv1a
   *       hash of the concatenated input under the same seed.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return m_h;
  }
};

}  // namespace nexenne::algorithm
