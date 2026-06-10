/**
 * @file
 * @brief Compute a value once on first use, via nexenne::utility::lazy.
 */

#include <print>
#include <string>

#include <nexenne/utility/lazy.hpp>

namespace {

// Pretend this is an expensive load we only want to pay for if used.
auto load_config() -> std::string {
  std::println("(loading config from disk...)");
  return std::string{"theme=dark;workers=4"};
}

}  // namespace

auto main() -> int {
  // Non-movable, thread-safe: the factory runs at most once under call_once.
  auto config{nexenne::utility::lazy{[] { return load_config(); }}};

  std::println("before access, has_value: {}", config.has_value());

  // First dereference fires the factory exactly once.
  std::string const& cfg{*config};
  std::println("config: {}", cfg);
  std::println("after access, has_value: {}", config.has_value());

  // Second access returns the cached value without re-running the factory.
  std::println("length: {}", config->size());

  return 0;
}
