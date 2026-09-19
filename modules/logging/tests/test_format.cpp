/**
 * @file
 * @brief Tests for the nexenne::logging formatting layers.
 */

#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>
#include <string_view>

#include <nexenne/logging/format.hpp>

namespace {

namespace lg = nexenne::logging;

// Checks that the formatter, to_string and operator<< all print the name.
template <typename E>
auto names_agree(E const value, std::string_view const name) -> bool {
  auto os{std::ostringstream{}};
  os << value;
  return lg::to_string(value) == name && std::format("{}", value) == name && os.str() == name;
}

TEST_CASE("nexenne::logging::level prints its padded name through every layer") {
  CHECK(names_agree(lg::level::info, "INFO "));
  CHECK(names_agree(lg::level::critical, "CRIT "));
  CHECK(std::format("[{:>6}]", lg::level::warn) == "[ WARN ]");
  static_assert(lg::to_string(lg::level::error) == "ERROR");
}

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS)

TEST_CASE("nexenne::logging host enums print their names through every layer") {
  CHECK(names_agree(lg::overflow_action::block, "block"));
  CHECK(names_agree(lg::overflow_action::drop_oldest, "drop_oldest"));
  CHECK(names_agree(lg::overflow_action::drop_newest, "drop_newest"));
  CHECK(names_agree(lg::console_sink::stream::stdout_only, "stdout_only"));
  CHECK(names_agree(lg::console_sink::stream::stderr_only, "stderr_only"));
  CHECK(names_agree(lg::console_sink::stream::auto_split, "auto_split"));
}

#endif

}  // namespace
