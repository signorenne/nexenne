/**
 * @file
 * @brief Reflect a state-machine enum with nexenne::utility::enum_to_string.
 */

#include <cstdint>
#include <print>
#include <string_view>

#include <nexenne/utility/enum_to_string.hpp>

// Reflect a small state machine's enum: list every state, name a runtime value,
// and parse a name back into a value, all without a hand-written table.
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
  // Compile-time facts.
  static_assert(nexenne::utility::enum_name<connection::connected>() == "connected");
  static_assert(nexenne::utility::enum_count<connection>() == 5);

  std::println("connection has {} states:", nexenne::utility::enum_count<connection>());
  for (auto const state : nexenne::utility::enum_values<connection>()) {
    std::println(
      "  {} = {}",
      static_cast<unsigned>(static_cast<std::uint8_t>(state)),
      nexenne::utility::enum_to_string(state)
    );
  }

  // Runtime value -> name.
  auto const current{connection::closing};
  std::println("current state: {}", nexenne::utility::enum_to_string(current));

  // Runtime name -> value, with a miss for good measure.
  for (auto const name : {std::string_view{"connected"}, std::string_view{"frobnicate"}}) {
    if (auto const parsed{nexenne::utility::enum_cast<connection>(name)}; parsed.has_value()) {
      std::println(
        "parsed '{}' -> {}", name, static_cast<unsigned>(static_cast<std::uint8_t>(*parsed))
      );
    } else {
      std::println("parsed '{}' -> <invalid>", name);
    }
  }

  return 0;
}
