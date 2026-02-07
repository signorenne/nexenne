#pragma once

/**
 * @file
 * @brief Reproducible seed-sequence helpers.
 *
 * Use these instead of \c std::seed_seq when you need a guaranteed-
 * stable sequence across toolchains (libstdc++'s \c seed_seq output
 * is implementation-defined). Each helper folds input bytes into a
 * 64-bit seed with per-byte SplitMix64 mixing, fast, no
 * library dependencies, identical output on every platform.
 *
 * Typical use:
 * \code
 * auto seed{seed_from_string("scene-42-physics")};
 * auto rng{xoshiro256ss{seed}};
 * \endcode
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace nexenne::random {

namespace detail {

// SplitMix64 finaliser (Sebastiano Vigna, public domain): a fast 64-bit mix.
[[nodiscard]] constexpr auto splitmix64(std::uint64_t x) noexcept -> std::uint64_t {
  x += 0x9E3779B97F4A7C15ULL;
  auto z{x};
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

}  // namespace detail

/**
 * @brief Hashes a byte sequence into a 64-bit seed.
 *
 * Starts from the FNV offset basis and folds each byte in with a
 * XOR-then-SplitMix64 step (not FNV-1a: there is no FNV-prime multiply),
 * giving the same result on every platform.
 *
 * @param bytes Bytes to hash.
 *
 * @return A 64-bit seed derived from \p bytes.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(bytes.size()).
 *
 * @warning Not cryptographically secure.
 */
[[nodiscard]] constexpr auto seed_from_bytes(std::span<std::byte const> bytes
) noexcept -> std::uint64_t {
  std::uint64_t state{0xCBF29CE484222325ULL};  // FNV offset basis as the starting mix
  for (auto const b : bytes) {
    state ^= static_cast<std::uint64_t>(b);
    state = detail::splitmix64(state);
  }
  return state;
}

/**
 * @brief Hashes a UTF-8 string into a 64-bit seed.
 *
 * Mixes each byte of \p s with the same scheme as \c seed_from_bytes,
 * so equal strings always map to equal seeds across toolchains.
 *
 * @param s String to hash.
 *
 * @return A 64-bit seed derived from \p s.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(s.size()).
 *
 * @warning Not cryptographically secure.
 */
[[nodiscard]] constexpr auto seed_from_string(std::string_view const s) noexcept -> std::uint64_t {
  std::uint64_t state{0xCBF29CE484222325ULL};
  for (auto const c : s) {
    state ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(c));
    state = detail::splitmix64(state);
  }
  return state;
}

/**
 * @brief Derives \c N independent seeds from a single master seed.
 *
 * Iterates SplitMix64 from \p master to fill the array, so a fixed
 * master deterministically produces the same set of seeds. Useful for
 * seeding one engine per thread or task without correlation.
 *
 * @tparam N Number of seeds to generate.
 * @param master Master seed driving the sequence.
 *
 * @return An array of \c N derived seeds.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(N).
 *
 * @warning Not cryptographically secure.
 */
template <std::size_t N>
[[nodiscard]] constexpr auto seed_sequence(std::uint64_t const master
) noexcept -> std::array<std::uint64_t, N> {
  std::array<std::uint64_t, N> out{};
  std::uint64_t state{master};
  for (std::size_t i{0}; i < N; ++i) {
    state = detail::splitmix64(state);
    out[i] = state;
  }
  return out;
}

}  // namespace nexenne::random
