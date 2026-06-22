/**
 * @file
 * @brief Ergonomic helpers over std::expected, via nexenne::utility::expected_utils.
 *
 * A device boot sequence runs several fallible steps. We thread their errors
 * through without a single throw, showing each helper in the header:
 *   - into_optional: drop the error channel when only success/value matters.
 *   - try_or:        supply a fallback computed from the error.
 *   - first_error:   fold a series of void-returning steps to the first failure.
 *   - flatten:       collapse a nested expected<expected<T, E>, E>.
 */

#include <expected>
#include <optional>
#include <print>
#include <string>

#include <nexenne/utility/expected_utils.hpp>

namespace util = nexenne::utility;

namespace {

using result = std::expected<int, std::string>;
using step = std::expected<void, std::string>;  // a fallible action with no value

auto parse_port(int raw) -> result {
  if (raw < 0 || raw > 65535) {
    return std::unexpected{std::string{"port out of range"}};
  }
  return raw;
}

// Three boot steps; the middle one fails so we can see first_error short-circuit.
auto open_bus() -> step {
  return {};
}

auto configure_clock() -> step {
  return std::unexpected{std::string{"clock PLL did not lock"}};
}

auto reset_chip() -> step {
  return {};
}

// A factory whose own lookup is fallible, returning a fallible value: a natural
// nested expected that flatten untangles into one level.
auto open_config() -> std::expected<result, std::string> {
  return parse_port(8080);  // outer ok, inner ok
}

}  // namespace

auto main() -> int {
  // into_optional: keep the value, discard why it failed.
  std::optional<int> const ok{util::into_optional(parse_port(8080))};
  std::optional<int> const bad{util::into_optional(parse_port(99999))};
  std::println("ok has value: {}, value: {}", ok.has_value(), ok.value_or(-1));
  std::println("bad has value: {}", bad.has_value());

  // try_or: the fallback is a callable that sees the error.
  auto const port{util::try_or(parse_port(70000), [](std::string const& e) {
    std::println("falling back ({})", e);
    return 8080;
  })};
  std::println("resolved port: {}", port);

  // first_error: run a sequence of void steps and stop at the first failure,
  // exactly like chaining `if (auto r = step(); !r) return r;` but as one call.
  if (auto const boot{util::first_error(open_bus(), configure_clock(), reset_chip())}; !boot) {
    std::println("boot failed at: {}", boot.error());
  } else {
    std::println("boot ok");
  }

  // flatten: an expected<expected<T, E>, E> becomes a flat expected<T, E>; an
  // error at either nesting level surfaces as the single error.
  if (auto const cfg{util::flatten(open_config())}) {
    std::println("config port: {}", *cfg);
  }

  return 0;
}
