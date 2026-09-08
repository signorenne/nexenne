/**
 * @file
 * @brief Compute a value once on first use, via nexenne::utility::lazy.
 *
 * A config load stands in for an expensive computation paid for only when the
 * value is used. The \c lazy is non-movable and thread-safe: its factory runs at
 * most once under \c std::call_once. The example:
 *
 *   1. shows that nothing is computed before the first access;
 *   2. races four threads on that first access, which \c call_once serialises,
 *      so a counter proves the factory ran exactly once and every thread read
 *      the same value;
 *   3. reads the cached value again without re-running the factory.
 */

#include <atomic>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include <nexenne/utility/lazy.hpp>

namespace {

std::atomic<int> g_loads{0};

auto load_config() -> std::string {
  g_loads.fetch_add(1, std::memory_order_relaxed);
  std::println("(loading config from disk...)");
  return std::string{"theme=dark;workers=4"};
}

}  // namespace

auto main() -> int {
  auto config{nexenne::utility::lazy{[] { return load_config(); }}};

  std::println("before access, has_value: {}", config.has_value());

  std::vector<std::jthread> racers;
  for (int i{0}; i < 4; ++i) {
    racers.emplace_back([&config] { [[maybe_unused]] std::string const& seen{*config}; });
  }
  racers.clear();  // join every thread before reading the counter

  std::println("config: {}", *config);
  std::println("after access, has_value: {}", config.has_value());
  std::println("factory ran {} time(s)", g_loads.load(std::memory_order_relaxed));

  std::println("length: {}", config->size());

  return 0;
}
