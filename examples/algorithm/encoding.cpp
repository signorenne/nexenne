/**
 * @file
 * @brief Example: the nexenne::algorithm byte encodings.
 *
 * Shows hex, Base64 (standard and URL-safe), Base32, URL percent-encoding, and
 * the heap-free COBS framing codec. The heap-allocating overloads are used for
 * brevity; each codec also has a span-in/span-out overload for embedded use.
 */

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/algorithm/encoding/base_n.hpp>
#include <nexenne/algorithm/encoding/cobs.hpp>
#include <nexenne/algorithm/encoding/url.hpp>

namespace alg = nexenne::algorithm;

[[nodiscard]] auto bytes_of(std::string_view const s) -> std::span<std::uint8_t const> {
  return {reinterpret_cast<std::uint8_t const*>(s.data()), s.size()};
}

auto main() -> int {
  constexpr std::string_view text{"foobar"};

  std::printf("hex        : %s\n", alg::hex_encode(bytes_of(text)).c_str());
  std::printf("base64     : %s\n", alg::base64_encode(bytes_of(text)).c_str());
  std::printf("base64url  : %s\n", alg::base64url_encode(bytes_of(text)).c_str());
  std::printf("base32     : %s\n", alg::base32_encode(bytes_of(text)).c_str());

  std::printf("url        : %s\n", alg::url_encode("a b+c/d").c_str());
  std::printf("form-url   : %s\n", alg::form_url_encode("a b+c/d").c_str());

  // Decode round-trips back to the original.
  auto const decoded{alg::base64_decode(alg::base64_encode(bytes_of(text)))};
  std::printf(
    "base64 round-trip ok: %s\n",
    (decoded.has_value()
     && *decoded == std::vector<std::uint8_t>{bytes_of(text).begin(), bytes_of(text).end()})
      ? "yes"
      : "no"
  );

  // COBS frames a payload containing a 0x00 so 0x00 can delimit packets.
  auto const payload{std::array<std::uint8_t, 4>{0x11, 0x00, 0x22, 0x00}};
  auto framed{std::vector<std::uint8_t>(alg::cobs_encoded_max_size(payload.size()))};
  auto const n{
    alg::cobs_encode(std::span<std::uint8_t const>{payload}, std::span<std::uint8_t>{framed})
  };
  framed.resize(n.value_or(0));
  std::printf("cobs framed: ");
  for (auto const b : framed) {
    std::printf("%02X ", b);
  }
  std::printf("(no 0x00 byte present)\n");
  return 0;
}
