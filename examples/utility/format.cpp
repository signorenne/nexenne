/**
 * @file
 * @brief Printing the nexenne::utility types with std::format via format.hpp.
 *
 * The type headers stay free of the format header; format.hpp adds the
 * formatters, so only the translation units that print pay for it. Each line
 * prints one utility type, with format specs where the type forwards them.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>

#include <nexenne/utility/buffer_cursor.hpp>
#include <nexenne/utility/cobs.hpp>
#include <nexenne/utility/flags.hpp>
#include <nexenne/utility/format.hpp>
#include <nexenne/utility/lazy.hpp>
#include <nexenne/utility/scope_guard.hpp>
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

  auto bytes{std::array<std::byte, 8>{}};
  auto cursor{util::buffer_cursor{std::span{bytes}}};
  cursor.advance(3);
  std::println("cur:   {}", cursor);
  auto const answer{util::lazy{[] { return 42; }}};
  std::println("lazy:  {}", answer);
  auto guard{util::scope_guard{[] {}}};
  guard.dismiss();
  std::println("guard: {}", guard);
  return 0;
}
