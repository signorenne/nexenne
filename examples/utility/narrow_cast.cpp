/**
 * @file
 * @brief Down-convert audio samples safely with nexenne::utility::narrow_cast.
 */

#include <cstdint>
#include <print>
#include <vector>

#include <nexenne/utility/narrow_cast.hpp>

namespace {

// Mix a stereo pair into a mono sample, then store it in a compact 16-bit buffer.
// Each averaged value is known to fit in std::int16_t, so the narrowing is
// checked rather than guessed at.
auto mix_to_mono(std::vector<std::int32_t> const& left, std::vector<std::int32_t> const& right)
  -> std::vector<std::int16_t> {
  std::vector<std::int16_t> mono;
  mono.reserve(left.size());
  for (std::size_t i{0}; i < left.size(); ++i) {
    auto const sum{left[i] + right[i]};
    auto const averaged{sum / 2};
    mono.push_back(nexenne::utility::narrow_cast<std::int16_t>(averaged));
  }
  return mono;
}

}  // namespace

auto main() -> int {
  std::vector<std::int32_t> const left{1000, -2000, 30000, -32000};
  std::vector<std::int32_t> const right{3000, -4000, 1000, -1000};
  auto const mono{mix_to_mono(left, right)};
  for (std::int16_t const sample : mono) {
    std::println("sample: {}", sample);
  }
  // sample: 2000
  // sample: -3000
  // sample: 15500
  // sample: -16500
  return 0;
}
