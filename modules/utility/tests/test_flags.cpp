/**
 * @file
 * @brief Tests for nexenne::utility::flags.
 */

#include <doctest/doctest.h>

#include <cstdint>

#include <nexenne/utility/flags.hpp>

namespace {

namespace util = nexenne::utility;

enum class perm : std::uint8_t {
  read = 1 << 0,
  write = 1 << 1,
  exec = 1 << 2
};

static_assert(util::flags<perm>{}.none());
static_assert(util::flags{perm::read}.has(perm::read));
static_assert(!util::flags{perm::read}.has(perm::write));

constexpr auto rw{util::flags{perm::read} | perm::write};
static_assert(rw.has(perm::read) && rw.has(perm::write) && !rw.has(perm::exec));
static_assert(rw.raw() == 0b011);
static_assert(util::flags<perm>::from_raw(std::uint8_t{0b011}) == rw);

constexpr auto rw_mask{static_cast<perm>(0b011)};
static_assert(util::flags{perm::read}.has_any(rw_mask));
static_assert(!util::flags{perm::read}.has_all(rw_mask));
static_assert(rw.has_all(rw_mask));                            // both bits set -> has_all true
static_assert(!util::flags{perm::read}.has_any(perm::write));  // disjoint -> has_any false

// Complement, intersection, symmetric difference, enum-on-left.
static_assert((~util::flags{perm::read}).has(perm::write));
static_assert(!(~util::flags{perm::read}).has(perm::read));
static_assert((~util::flags{perm::read}).raw() == 0xFE);  // narrow-underlying complement
static_assert((rw & util::flags{perm::write}) == util::flags{perm::write});
static_assert((util::flags{perm::read} ^ util::flags{perm::read}).none());
static_assert((perm::read | util::flags{perm::write}) == rw);  // enum | flags
static_assert(util::flags<perm>::from_raw(std::uint8_t{0xFF}).raw() == 0xFF);

TEST_CASE("nexenne::utility::flags mutate in place and chain") {
  auto f{util::flags<perm>{}};
  f.set(perm::read).set(perm::exec);
  CHECK(f.has(perm::read));
  CHECK(f.has(perm::exec));
  CHECK_FALSE(f.has(perm::write));

  f.clear(perm::exec);
  CHECK_FALSE(f.has(perm::exec));

  f.toggle(perm::write);
  CHECK(f.has(perm::write));
  f.toggle(perm::write);
  CHECK_FALSE(f.has(perm::write));  // toggle twice restores

  f.clear_all();
  CHECK(f.none());
  CHECK_FALSE(f.any());
}

}  // namespace
