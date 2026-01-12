/**
 * @file
 * @brief Format a source location into a buffer, via source_location_format.
 */

#include <array>
#include <print>
#include <source_location>
#include <string_view>

#include <nexenne/utility/source_location_format.hpp>

namespace {

// A tiny logger that tags each line with where it was called from.
auto log_here(
  std::string_view msg, std::source_location const loc = std::source_location::current()
) -> void {
  std::array<char, 128> buf{};
  auto const tag{nexenne::utility::format_short(loc, buf)};
  std::println("[{}] {}", tag, msg);
}

}  // namespace

auto main() -> int {
  log_here("starting up");
  log_here("still running");

  // The long form also appends the enclosing function name.
  std::array<char, 256> buf{};
  auto const detailed{nexenne::utility::format_long(std::source_location::current(), buf)};
  std::println("detailed: {}", detailed);

  return 0;
}
