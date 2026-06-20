/**
 * @file
 * @brief Print every severity level with its fixed-width and single-char names.
 */

#include <cstdio>

#include <nexenne/logging/level.hpp>

auto main() -> int {
  namespace lg = nexenne::logging;
  for (auto const l :
       {lg::level::trace,
        lg::level::debug,
        lg::level::info,
        lg::level::warn,
        lg::level::error,
        lg::level::critical}) {
    std::printf("[%c] %.*s\n", lg::to_char(l), 5, lg::to_string(l).data());
  }
  return 0;
}
