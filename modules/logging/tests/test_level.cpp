/**
 * @file
 * @brief Tests for nexenne::logging level naming and ordering.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <string_view>

#include <nexenne/logging/level.hpp>

namespace {

namespace lg = nexenne::logging;

TEST_CASE("nexenne::logging::level is ordered by ascending severity") {
  // Verbosity increases as the numeric value decreases; off is the maximum.
  CHECK(static_cast<std::uint8_t>(lg::level::trace) == 0);
  CHECK(lg::level::trace < lg::level::debug);
  CHECK(lg::level::debug < lg::level::info);
  CHECK(lg::level::info < lg::level::warn);
  CHECK(lg::level::warn < lg::level::error);
  CHECK(lg::level::error < lg::level::critical);
  CHECK(lg::level::critical < lg::level::off);
}

TEST_CASE("nexenne::logging::to_string names every level in a fixed five-char column") {
  CHECK(lg::to_string(lg::level::trace) == "TRACE");
  CHECK(lg::to_string(lg::level::debug) == "DEBUG");
  CHECK(lg::to_string(lg::level::info) == "INFO ");
  CHECK(lg::to_string(lg::level::warn) == "WARN ");
  CHECK(lg::to_string(lg::level::error) == "ERROR");
  CHECK(lg::to_string(lg::level::critical) == "CRIT ");
  CHECK(lg::to_string(lg::level::off) == "OFF  ");
  // Every name is exactly five characters wide so columns align.
  for (auto const l :
       {lg::level::trace,
        lg::level::debug,
        lg::level::info,
        lg::level::warn,
        lg::level::error,
        lg::level::critical,
        lg::level::off}) {
    CHECK(lg::to_string(l).size() == 5);
  }
  // An out-of-range value is named, not undefined.
  CHECK(lg::to_string(static_cast<lg::level>(99)) == "?????");
}

TEST_CASE("nexenne::logging::to_char gives a one-character tag per level") {
  CHECK(lg::to_char(lg::level::trace) == 'T');
  CHECK(lg::to_char(lg::level::debug) == 'D');
  CHECK(lg::to_char(lg::level::info) == 'I');
  CHECK(lg::to_char(lg::level::warn) == 'W');
  CHECK(lg::to_char(lg::level::error) == 'E');
  CHECK(lg::to_char(lg::level::critical) == 'C');
  CHECK(lg::to_char(lg::level::off) == ' ');
  CHECK(lg::to_char(static_cast<lg::level>(99)) == '?');
}

TEST_CASE("nexenne::logging level naming is usable in a constant expression") {
  static_assert(lg::to_string(lg::level::info) == "INFO ");
  static_assert(lg::to_char(lg::level::error) == 'E');
}

TEST_CASE("nexenne::logging NEXENNE_LOG_MIN_LEVEL defaults to trace and gates by comparison") {
  // Not pre-defined in this TU, so the header's default applies: everything in.
  static_assert(NEXENNE_LOG_MIN_LEVEL == lg::level::trace);
  // The compile-time gate is a plain ordered comparison against the threshold.
  static_assert(lg::level::trace >= NEXENNE_LOG_MIN_LEVEL);     // trace is compiled in
  static_assert(lg::level::critical >= NEXENNE_LOG_MIN_LEVEL);  // and so is everything above
  // Emulate a stricter build-time threshold and confirm the gate would strip the
  // more verbose levels while keeping the severe ones.
  constexpr auto strict{lg::level::warn};
  static_assert(lg::level::debug < strict);   // gated out
  static_assert(lg::level::error >= strict);  // kept
}

}  // namespace
