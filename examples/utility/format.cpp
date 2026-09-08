/**
 * @file
 * @brief Printing the nexenne::utility types with std::format via format.hpp.
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/cobs.hpp>
#include <nexenne/utility/flags.hpp>
#include <nexenne/utility/format.hpp>
#include <nexenne/utility/static_string.hpp>
#include <nexenne/utility/strong_typedef.hpp>

namespace {

namespace util = nexenne::utility;

enum class led : std::uint8_t {
  red = 1U << 0U,
  green = 1U << 1U,
};

using celsius = util::strong_typedef<struct celsius_tag, double>;

}  // namespace

auto main() -> int {
  std::println("error: {}", util::cobs::error::output_too_small);
  std::println("leds:  {:#04b}", util::flags{led::red} | led::green);
  std::println("name:  [{:>6}]", util::static_string{"probe"});
  std::println("temp:  {:.1f}", celsius{21.5});
  std::println("op:    {}", util::ability::comparable);
  return 0;
}
