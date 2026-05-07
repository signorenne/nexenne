/**
 * @file
 * @brief Ergonomic helpers over std::expected, via nexenne::utility::expected_utils.
 *
 * A service reads its configuration and validates it without a single throw,
 * showing each helper in the header:
 *   - into_optional: drop the error channel when only success/value matters.
 *   - try_or:        supply a fallback computed from the error.
 *   - first_error:   fold independent validation results to the first failure.
 *   - flatten:       collapse a nested expected<expected<T, E>, E>.
 */

#include <expected>
#include <optional>
#include <print>
#include <string>
#include <string_view>

#include <nexenne/utility/expected_utils.hpp>

namespace util = nexenne::utility;

namespace {

using result = std::expected<int, std::string>;
using check = std::expected<void, std::string>;  // a validation with no value

auto parse_port(int raw) -> result {
  if (raw < 0 || raw > 65535) {
    return std::unexpected{std::string{"port out of range"}};
  }
  return raw;
}

// Three independent validations over an already-loaded config. They are safe
// to run eagerly and in any order: each only inspects data, none has a side
// effect that would be wrong to perform after another check failed.
auto check_name(std::string_view name, int* ran) -> check {
  ++*ran;
  if (name.empty()) {
    return std::unexpected{std::string{"name is empty"}};
  }
  return {};
}

auto check_port(int port, int* ran) -> check {
  ++*ran;
  if (port < 1024) {
    return std::unexpected{std::string{"port is privileged"}};
  }
  return {};
}

auto check_threads(int threads, int* ran) -> check {
  ++*ran;
  if (threads < 1) {
    return std::unexpected{std::string{"thread count must be positive"}};
  }
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

  // first_error: fold several independent validation results to the first
  // failure. Every argument is an ordinary function argument, so ALL of the
  // checks run before first_error even starts; the fold only selects among
  // the already-built results. That is why the inputs must be independent
  // checks like these, never steps that would be unsafe to run after an
  // earlier failure. For stop-on-first-error sequencing, chain and_then.
  int ran{0};
  auto const validated{util::first_error(
    check_name("api-server", &ran), check_port(80, &ran), check_threads(4, &ran)
  )};
  if (!validated) {
    std::println("config rejected: {}", validated.error());
  }
  std::println("checks run: {} (all three, despite the failure at the second)", ran);

  // flatten: an expected<expected<T, E>, E> becomes a flat expected<T, E>; an
  // error at either nesting level surfaces as the single error.
  if (auto const cfg{util::flatten(open_config())}) {
    std::println("config port: {}", *cfg);
  }

  return 0;
}
