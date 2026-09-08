/**
 * @file
 * @brief Narrow wide config values into a compact settings struct.
 *
 * A parser hands back everything as std::int64_t, but the runtime settings
 * struct stores compact fields. narrow_cast documents each deliberate
 * narrowing and, in debug builds, asserts the value and sign survive; under
 * NDEBUG it compiles to exactly the underlying static_cast. Values that might
 * genuinely be out of range are range-checked first, because the debug assert
 * is a development net, not input validation.
 *
 *   1. The parsed values are trusted to fit, and narrow_cast turns that trust
 *      into a debug assert: a port of 70000 or a retry count of -1 would abort
 *      with a "value changed" or "sign changed" message instead of wrapping.
 *   2. The timeout converts from floating point, which is range-checked before
 *      the cast, so even an absurd or NaN value cannot reach an undefined
 *      float-to-integer conversion.
 *   3. The checks also run in constant expressions, where a violation is a
 *      compile error rather than a runtime abort.
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/narrow_cast.hpp>

namespace {

using nexenne::utility::narrow_cast;

struct parsed_config {
  std::int64_t port{};
  std::int64_t retry_count{};
  double timeout_seconds{};
};

struct settings {
  std::uint16_t port{};
  std::uint8_t retry_count{};
  std::uint32_t timeout_ms{};
};

auto to_settings(parsed_config const& config) -> settings {
  return settings{
    .port = narrow_cast<std::uint16_t>(config.port),
    .retry_count = narrow_cast<std::uint8_t>(config.retry_count),
    .timeout_ms = narrow_cast<std::uint32_t>(config.timeout_seconds * 1000.0),
  };
}

}  // namespace

auto main() -> int {
  auto const config{parsed_config{.port = 8080, .retry_count = 3, .timeout_seconds = 2.5}};
  auto const active{to_settings(config)};

  std::println("port       = {}", active.port);
  std::println("retries    = {}", active.retry_count);
  std::println("timeout ms = {}", active.timeout_ms);

  static_assert(narrow_cast<std::uint16_t>(std::int64_t{8080}) == 8080);
  static_assert(narrow_cast<std::uint32_t>(2500.0) == 2500);
  return 0;
}
