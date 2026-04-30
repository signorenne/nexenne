/**
 * @file
 * @brief Compute a value once on first use, via nexenne::utility::lazy.
 */

#include <atomic>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include <nexenne/utility/lazy.hpp>

namespace {

std::atomic<int> g_loads{0};

// Pretend this is an expensive load we only want to pay for if the value is
// used. The counter proves the factory body runs exactly once, even when
// several threads reach the first access at the same time.
auto load_config() -> std::string {
  g_loads.fetch_add(1, std::memory_order_relaxed);
  std::println("(loading config from disk...)");
  return std::string{"theme=dark;workers=4"};
}

}  // namespace

auto main() -> int {
  // Non-movable, thread-safe: the factory runs at most once under call_once.
  auto config{nexenne::utility::lazy{[] { return load_config(); }}};

  std::println("before access, has_value: {}", config.has_value());

  // Four threads race on the first access. call_once serialises them, so the
  // factory still runs exactly once and every thread reads the same value.
  std::vector<std::jthread> racers;
  for (int i{0}; i < 4; ++i) {
    racers.emplace_back([&config] { [[maybe_unused]] std::string const& seen{*config}; });
  }
  racers.clear();  // join every thread before reading the counter

  std::println("config: {}", *config);
  std::println("after access, has_value: {}", config.has_value());
  std::println("factory ran {} time(s)", g_loads.load(std::memory_order_relaxed));

  // A later access returns the cached value without re-running the factory.
  std::println("length: {}", config->size());

  return 0;
}
