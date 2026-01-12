/**
 * @file
 * @brief A type-safe permission bitfield with nexenne::utility::flags.
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/flags.hpp>

// A type-safe permission bitfield. The scoped enum keeps unrelated bitmasks
// from mixing, and flags<E> gives readable set/has/operators without leaking
// into raw int arithmetic.
namespace {

enum class perm : std::uint8_t {
  read = 1U << 0U,
  write = 1U << 1U,
  exec = 1U << 2U,
  rwx = read | write | exec,  // combined mask for has_all / has_any tests
};

using perms = nexenne::utility::flags<perm>;

}  // namespace

auto main() -> int {
  // Build the owner's permissions by chaining and by operator|.
  auto owner{perms{}};
  owner.set(perm::read).set(perm::write);
  auto const full{owner | perm::exec};

  std::println("owner can write: {}", owner.has(perm::write));
  std::println("owner can exec:  {}", owner.has(perm::exec));
  std::println("full has all rwx: {}", full.has_all(perm::rwx));

  // Derive a read-only view, then toggle a bit back on.
  auto readonly{full};
  readonly.clear(perm::write).clear(perm::exec);
  std::println("readonly any of w/x: {}", readonly.has_any(perm::rwx));

  readonly.toggle(perm::exec);
  std::println("after toggle exec:   {}", readonly.has(perm::exec));

  // Raw round-trip for serialisation, plus complement.
  auto const wire{full.raw()};
  auto const restored{perms::from_raw(wire)};
  std::println(
    "raw bits: 0b{:03b}  round-trips: {}", static_cast<unsigned>(wire), restored == full
  );
  std::println("complement is empty: {}", (~full & full) == perms{} && full.any());

  return 0;
}
