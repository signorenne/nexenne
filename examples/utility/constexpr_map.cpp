/**
 * @file
 * @brief A compile-time HTTP status-code lookup table with constexpr_map.
 */

#include <array>
#include <print>
#include <string_view>
#include <utility>

#include <nexenne/utility/constexpr_map.hpp>

// A tiny HTTP status-code table resolved entirely at compile time. The entries
// are listed out of order on purpose; constexpr_map sorts them by key, so
// lookups are O(log N) binary searches with no runtime hash table.
namespace {

using std::string_view;

constexpr auto status_text{nexenne::utility::constexpr_map{std::array{
  std::pair{404, string_view{"Not Found"}},
  std::pair{200, string_view{"OK"}},
  std::pair{500, string_view{"Internal Server Error"}},
  std::pair{301, string_view{"Moved Permanently"}},
  std::pair{204, string_view{"No Content"}},
}}};

}  // namespace

auto main() -> int {
  // Compile-time guarantees baked into the binary.
  static_assert(status_text.size() == 5);
  static_assert(status_text.contains(200));
  static_assert(status_text[404] == string_view{"Not Found"});

  std::println("status table ({} entries, sorted by code):", status_text.size());
  for (auto const& [code, text] : status_text) {
    std::println("  {} -> {}", code, text);
  }

  auto const probe{std::array{200, 301, 418}};
  for (auto const code : probe) {
    if (auto const* const text{status_text.find(code)}; text != nullptr) {
      std::println("lookup {}: {}", code, *text);
    } else {
      std::println("lookup {}: <unknown>", code);
    }
  }

  return 0;
}
