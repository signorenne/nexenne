/**
 * @file
 * @brief Pack and unpack a hardware-style register, then iterate its flags.
 *
 * Models a 16-bit control register with a 4-bit channel field, a 6-bit
 * threshold field, and enable/error flag bits, using pack/extract and the
 * single-bit helpers.
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/bit.hpp>

namespace util = nexenne::utility;

enum : std::size_t {
  channel_offset = 0,
  channel_width = 4,
  threshold_offset = 4,
  threshold_width = 6,
  enable_bit = 10,
  error_bit = 11,
};

auto main() -> int {
  auto reg{std::uint16_t{0}};
  reg = util::pack_bits<std::uint16_t>(reg, 5, channel_offset, channel_width);
  reg = util::pack_bits<std::uint16_t>(reg, 42, threshold_offset, threshold_width);
  reg = util::set_bit<std::uint16_t>(reg, enable_bit);

  auto const channel{util::extract_bits<std::uint16_t>(reg, channel_offset, channel_width)};
  auto const threshold{util::extract_bits<std::uint16_t>(reg, threshold_offset, threshold_width)};

  static_assert(
    util::set_bits_mask<std::uint8_t>(2, 4) == 0b0001'1100, "contiguous mask is constexpr"
  );

  std::println("register = 0b{:016b}", reg);
  std::println("channel = {}, threshold = {}", channel, threshold);
  std::println(
    "enabled = {}, error = {}", util::test_bit(reg, enable_bit), util::test_bit(reg, error_bit)
  );

  std::print("set bit positions:");
  util::for_each_set_bit(reg, [](std::size_t const i) { std::print(" {}", i); });
  std::println("");
  return 0;
}
