/**
 * @file
 * @brief Reflect a state-machine enum with nexenne::utility::enum_to_string.
 *
 * A small connection state machine is reflected without a hand-written table:
 *
 *   1. compile-time facts: the name of one enumerator and the state count;
 *   2. every state listed with its value and name;
 *   3. a runtime value turned into its name;
 *   4. names parsed back into values, including one that does not match.
 */

#include <cstdint>
#include <print>
#include <string_view>
#include <utility>

#include <nexenne/utility/enum_to_string.hpp>

namespace {

enum class connection : std::uint8_t {
  idle = 0,
  connecting = 1,
  connected = 2,
  closing = 3,
  closed = 4,
};

}  // namespace

auto main() -> int {
  static_assert(nexenne::utility::enum_name<connection::connected>() == "connected");
  static_assert(nexenne::utility::enum_count<connection>() == 5);

  std::println("connection has {} states:", nexenne::utility::enum_count<connection>());
  for (auto const state : nexenne::utility::enum_values<connection>()) {
    std::println("  {} = {}", std::to_underlying(state), nexenne::utility::enum_to_string(state));
  }

  auto const current{connection::closing};
  std::println("current state: {}", nexenne::utility::enum_to_string(current));

  for (auto const name : {std::string_view{"connected"}, std::string_view{"frobnicate"}}) {
    if (auto const parsed{nexenne::utility::enum_cast<connection>(name)}; parsed.has_value()) {
      std::println("parsed '{}' -> {}", name, std::to_underlying(*parsed));
    } else {
      std::println("parsed '{}' -> <invalid>", name);
    }
  }

  return 0;
}
