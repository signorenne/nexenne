/**
 * @file
 * @brief Ergonomic helpers over std::expected, via nexenne::utility::expected_utils.
 *
 * A service reads its configuration and validates it without a single throw,
 * showing each helper in the header:
 *
 *   1. \c into_optional keeps a port value and drops why parsing failed.
 *   2. \c try_or supplies a fallback port computed from the error.
 *   3. \c first_error folds three independent validations to the first failure.
 *      Every argument is evaluated before the fold runs (the run count proves
 *      it), so the inputs must be checks that are safe to run eagerly; chain
 *      \c and_then for stop-on-first-error sequencing.
 *   4. \c flatten collapses a fallible lookup returning a fallible value into
 *      one level, where an error at either level surfaces as the single error.
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
using check = std::expected<void, std::string>;

auto parse_port(int raw) -> result {
  if (raw < 0 || raw > 65535) {
    return std::unexpected{std::string{"port out of range"}};
  }
  return raw;
}

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

auto open_config() -> std::expected<result, std::string> {
  return parse_port(8080);
}

}  // namespace

auto main() -> int {
  std::optional<int> const ok{util::into_optional(parse_port(8080))};
  std::optional<int> const bad{util::into_optional(parse_port(99999))};
  std::println("ok has value: {}, value: {}", ok.has_value(), ok.value_or(-1));
  std::println("bad has value: {}", bad.has_value());

  auto const port{util::try_or(parse_port(70000), [](std::string const& e) {
    std::println("falling back ({})", e);
    return 8080;
  })};
  std::println("resolved port: {}", port);

  int ran{0};
  auto const validated{
    util::first_error(check_name("api-server", &ran), check_port(80, &ran), check_threads(4, &ran))
  };
  if (!validated) {
    std::println("config rejected: {}", validated.error());
  }
  std::println("checks run: {} (all three, despite the failure at the second)", ran);

  if (auto const cfg{util::flatten(open_config())}) {
    std::println("config port: {}", *cfg);
  }

  return 0;
}
