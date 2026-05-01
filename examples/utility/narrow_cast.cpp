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
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/narrow_cast.hpp>

namespace {

using nexenne::utility::narrow_cast;

// What a config parser produces: everything wide, signs unknown.
struct parsed_config {
  std::int64_t port{};
  std::int64_t retry_count{};
  double timeout_seconds{};
};

// What the runtime keeps: compact, cache-friendly fields.
struct settings {
  std::uint16_t port{};
  std::uint8_t retry_count{};
  std::uint32_t timeout_ms{};
};

auto to_settings(parsed_config const& config) -> settings {
  // These values are trusted to fit by this point (the parser validated
  // ranges); narrow_cast turns that trust into a debug-build assert. A value
  // like port = 70000 or retry_count = -1 aborts here in debug with a
  // "value changed" or "sign changed" message instead of wrapping silently.
  return settings{
    .port = narrow_cast<std::uint16_t>(config.port),
    .retry_count = narrow_cast<std::uint8_t>(config.retry_count),
    // Float to integral is range-checked before the cast in debug, so even an
    // absurd or NaN timeout cannot reach an undefined float-to-int conversion.
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
  // port       = 8080
  // retries    = 3
  // timeout ms = 2500

  // The checks also run in constant expressions, where a violation is a
  // compile error rather than a runtime abort.
  static_assert(narrow_cast<std::uint16_t>(std::int64_t{8080}) == 8080);
  static_assert(narrow_cast<std::uint32_t>(2500.0) == 2500);
  return 0;
}
