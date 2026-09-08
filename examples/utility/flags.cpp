/**
 * @file
 * @brief A type-safe permission bitfield with nexenne::utility::flags.
 *
 * The scoped enum keeps unrelated bitmasks from mixing, and \c flags gives
 * readable set, has, and bitwise operators without leaking into raw integer
 * arithmetic. The example:
 *
 *   1. builds the owner's permissions by chaining \c set and by \c operator|,
 *      with the enum on either side of the operator;
 *   2. queries the mask: \c has and \c has_all need every bit of the argument,
 *      \c has_any at least one, and \c count reports how many bits are on;
 *   3. derives a read-only view with \c clear, then toggles a bit back on;
 *   4. drops an intersection straight into an if condition through the
 *      explicit bool conversion (exactly \c any);
 *   5. prints the raw mask with integer format specs, no hand cast of \c raw;
 *   6. round-trips the raw value for serialisation and takes the complement.
 */

#include <cstdint>
#include <print>

#include <nexenne/utility/flags.hpp>
#include <nexenne/utility/format.hpp>

namespace {

enum class perm : std::uint8_t {
  read = 1U << 0U,
  write = 1U << 1U,
  exec = 1U << 2U,
  rwx = read | write | exec,
};

using perms = nexenne::utility::flags<perm>;

}  // namespace

auto main() -> int {
  auto owner{perms{}};
  owner.set(perm::read).set(perm::write);
  auto const full{perm::exec | owner};

  std::println("owner can write: {}", owner.has(perm::write));
  std::println("owner can exec:  {}", owner.has(perm::exec));
  std::println("full has all rwx: {}", full.has_all(perm::rwx));
  std::println("full sets {} of {} bits", full.count(), perms{perm::rwx}.count());

  auto readonly{full};
  readonly.clear(perm::write).clear(perm::exec);
  std::println("readonly any of w/x: {}", readonly.has_any(perm::rwx));

  readonly.toggle(perm::exec);
  std::println("after toggle exec:   {}", readonly.has(perm::exec));

  if (auto const elevated{readonly & perm::exec}) {
    std::println("readonly still executes: {} extra bit(s)", elevated.count());
  }

  std::println("full mask: {:#05b} (decimal {})", full, full);

  auto const restored{perms::from_raw(full.raw())};
  std::println("raw round-trips: {}", restored == full);
  std::println("complement is empty: {}", (~full & full) == perms{} && full.any());

  return 0;
}
