/**
 * @file
 * @brief Ergonomic helpers over std::expected, via nexenne::utility::expected_utils.
 */

#include <expected>
#include <optional>
#include <print>
#include <string>

#include <nexenne/utility/expected_utils.hpp>

namespace {

using result = std::expected<int, std::string>;

auto parse_port(int raw) -> result {
  if (raw < 0 || raw > 65535) {
    return std::unexpected{std::string{"port out of range"}};
  }
  return raw;
}

}  // namespace

auto main() -> int {
  using namespace nexenne::utility;

  // into_optional: drop the error channel.
  std::optional<int> const ok{into_optional(parse_port(8080))};
  std::optional<int> const bad{into_optional(parse_port(99999))};
  std::println("ok has value: {}, value: {}", ok.has_value(), ok.value_or(-1));
  std::println("bad has value: {}", bad.has_value());

  // try_or: supply a fallback computed from the error.
  auto const port{try_or(parse_port(70000), [](std::string const& e) {
    std::println("falling back ({})", e);
    return 8080;
  })};
  std::println("resolved port: {}", port);

  return 0;
}
