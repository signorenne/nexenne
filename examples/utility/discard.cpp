/**
 * @file
 * @brief Drop values on purpose with nexenne::utility::discard.
 */

#include <cstddef>
#include <print>
#include <string>

#include <nexenne/utility/discard.hpp>

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
  nexenne::utility::discard(try_reserve(buffer, 1024));
  std::println("capacity is at least 1024: {}", buffer.capacity() >= 1024);

  // discard evaluates every argument, so side effects still happen: both
  // [[nodiscard]] calls run and both results are dropped in one statement.
  nexenne::utility::discard(bump(), bump());
  std::println("side effects ran: {}", g_side_effects);

  // For a named declaration that merely might go unused, reach for the standard
  // [[maybe_unused]] attribute instead; discard is for an unnamed result.
  [[maybe_unused]] std::size_t const cap{buffer.capacity()};

  return 0;
}
