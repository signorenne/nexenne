/**
 * @file
 * @brief Drop values on purpose with nexenne::utility::ignore.
 *
 *   1. A [[nodiscard]] reserve call is made only for its side effect, and
 *      \c ignore says so without the banned (void) or \c static_cast<void>
 *      spellings.
 *   2. \c ignore evaluates every argument, so two [[nodiscard]] calls both run
 *      and both results are dropped in one statement.
 *   3. A named declaration that merely might go unused takes the standard
 *      [[maybe_unused]] attribute instead; \c ignore is for an unnamed result.
 */

#include <cstddef>
#include <print>
#include <string>

#include <nexenne/utility/ignore.hpp>

namespace {

// A [[nodiscard]] API: the caller is expected to inspect the result. Here we
// call it only for its side effect (growing the buffer) and want to say so.
[[nodiscard]] auto try_reserve(std::string& buffer, std::size_t const bytes) -> bool {
  buffer.reserve(bytes);
  return buffer.capacity() >= bytes;
}

int g_side_effects{0};

[[nodiscard]] auto bump() -> int {
  return ++g_side_effects;
}

}  // namespace

auto main() -> int {
  std::string buffer{"log"};

  // Drop a [[nodiscard]] result we do not need. Without discard this warns, and
  // the banned alternatives are (void)try_reserve(...) or static_cast<void>(...).
  nexenne::utility::ignore(try_reserve(buffer, 1024));
  std::println("capacity is at least 1024: {}", buffer.capacity() >= 1024);

  // ignore evaluates every argument, so side effects still happen: both
  // [[nodiscard]] calls run and both results are dropped in one statement.
  nexenne::utility::ignore(bump(), bump());
  std::println("side effects ran: {}", g_side_effects);

  // For a named declaration that merely might go unused, reach for the standard
  // [[maybe_unused]] attribute instead; ignore is for an unnamed result.
  [[maybe_unused]] std::size_t const cap{buffer.capacity()};

  return 0;
}
