/**
 * @file
 * @brief Example: the nexenne::algorithm checksums (Adler-32 and CRC).
 *
 * Shows Adler-32 (and its seed-chaining), the named CRC presets, the streaming
 * CRC context, and a custom CRC defined by a Rocksoft crc_spec literal. None of
 * these are cryptographically secure; they are integrity checks.
 */

#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include <nexenne/algorithm/checksum/adler32.hpp>
#include <nexenne/algorithm/checksum/crc.hpp>

namespace alg = nexenne::algorithm;

auto main() -> int {
  constexpr std::string_view text{"123456789"};

  std::printf("adler32(\"123456789\") = 0x%08x\n", alg::adler32(text));
  std::printf("crc32   (\"123456789\") = 0x%08x\n", alg::crc32(text));
  std::printf("crc32c  (\"123456789\") = 0x%08x\n", alg::crc32c(text));
  std::printf("crc8    (\"123456789\") = 0x%02x\n", alg::crc8(text));

  // Adler-32 chains: hashing the halves and feeding the first result as the
  // seed for the second matches hashing the whole.
  auto const half{alg::adler32(std::string_view{"12345"})};
  std::printf(
    "adler32 chained      = 0x%08x  (matches: %s)\n",
    alg::adler32(std::string_view{"6789"}, half),
    alg::adler32(std::string_view{"6789"}, half) == alg::adler32(text) ? "yes" : "no"
  );

  // The streaming CRC context accepts chunked input.
  alg::crc_ctx<alg::crc32_ieee_spec> ctx;
  ctx.update(std::string_view{"1234"});
  ctx.update(std::string_view{"56789"});
  std::printf(
    "crc32 streamed       = 0x%08x  (matches: %s)\n",
    ctx.value(),
    ctx.value() == alg::crc32(text) ? "yes" : "no"
  );

  // A custom CRC: any of the ~100 catalogued models is one crc_spec away.
  constexpr auto cdma2000{alg::crc_spec<16>{
    .poly = 0xC867, .init = 0xFFFF, .ref_in = false, .ref_out = false, .xor_out = 0x0000
  }};
  std::printf("crc16/cdma2000       = 0x%04x\n", alg::crc<cdma2000>(text));
  return 0;
}
