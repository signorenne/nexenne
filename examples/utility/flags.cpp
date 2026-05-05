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
  // Build the owner's permissions by chaining and by operator|. The enum works
  // on either side of the operator, so composition reads naturally.
  auto owner{perms{}};
  owner.set(perm::read).set(perm::write);
  auto const full{perm::exec | owner};

  // Query the mask: has / has_all require every bit of the argument, has_any
  // needs at least one, and count() reports how many bits are on.
  std::println("owner can write: {}", owner.has(perm::write));
  std::println("owner can exec:  {}", owner.has(perm::exec));
  std::println("full has all rwx: {}", full.has_all(perm::rwx));
  std::println("full sets {} of {} bits", full.count(), perms{perm::rwx}.count());

  // Derive a read-only view, then toggle a bit back on.
  auto readonly{full};
  readonly.clear(perm::write).clear(perm::exec);
  std::println("readonly any of w/x: {}", readonly.has_any(perm::rwx));

  readonly.toggle(perm::exec);
  std::println("after toggle exec:   {}", readonly.has(perm::exec));

  // The explicit bool conversion (exactly any()) drops an intersection
  // straight into an if condition.
  if (auto const elevated{readonly & perm::exec}) {
    std::println("readonly still executes: {} extra bit(s)", elevated.count());
  }

  // The std::formatter prints the raw mask and forwards integer format specs,
  // so showing the bits needs no hand-cast of raw().
  std::println("full mask: {:#05b} (decimal {})", full, full);

  // Raw round-trip for serialisation, plus complement.
  auto const restored{perms::from_raw(full.raw())};
  std::println("raw round-trips: {}", restored == full);
  std::println("complement is empty: {}", (~full & full) == perms{} && full.any());

  return 0;
}
