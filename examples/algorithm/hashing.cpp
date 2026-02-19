/**
 * @file
 * @brief Example: the nexenne::algorithm non-cryptographic hashes.
 *
 * Shows FNV-1a (tiny, constexpr), MurmurHash3 (high quality, 32- and 128-bit),
 * and xxHash (fast, with a streaming context). None are cryptographically
 * secure; they are for hash tables, fingerprints, and content addressing.
 */

#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include <nexenne/algorithm/hash/fnv.hpp>
#include <nexenne/algorithm/hash/murmur.hpp>
#include <nexenne/algorithm/hash/xxhash.hpp>

namespace alg = nexenne::algorithm;

auto main() -> int {
  constexpr std::string_view text{"the quick brown fox"};

  // FNV-1a is constexpr: a compile-time string identifier.
  constexpr auto id{alg::fnv1a<64>(std::string_view{"state.idle"})};
  std::printf(
    "fnv1a<64>(\"state.idle\") = 0x%016llx  (compile time)\n", static_cast<unsigned long long>(id)
  );

  std::printf("fnv1a<32>     = 0x%08x\n", alg::fnv1a<32>(text));
  std::printf("murmur3<32>   = 0x%08x\n", alg::murmur3<32>(text));

  auto const m128{alg::murmur3<128>(text)};
  std::printf(
    "murmur3<128>  = 0x%016llx%016llx\n",
    static_cast<unsigned long long>(m128[0]),
    static_cast<unsigned long long>(m128[1])
  );

  std::printf(
    "xxhash<64>    = 0x%016llx\n", static_cast<unsigned long long>(alg::xxhash<64>(text))
  );

  // The streaming context hashes chunked input and matches the one-shot result.
  alg::xxhash_ctx<64> ctx;
  ctx.update(std::string_view{"the quick "});
  ctx.update(std::string_view{"brown fox"});
  std::printf(
    "xxhash<64> streamed = 0x%016llx  (matches: %s)\n",
    static_cast<unsigned long long>(ctx.value()),
    ctx.value() == alg::xxhash<64>(text) ? "yes" : "no"
  );
  return 0;
}
