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

// Compound assignment operators, both the flags and the enumerator overloads,
// at compile time.
static_assert([] {
  auto x{util::flags<perm>{}};
  x |= perm::read;                // enum overload
  x |= util::flags{perm::write};  // flags overload
  return x.raw();
}() == 0b011);
static_assert([] {
  auto x{rw};
  x &= perm::read;  // keep only read
  return x.raw();
}() == 0b001);
static_assert([] {
  auto x{rw};
  x ^= util::flags{perm::read};  // drop read
  return x.raw();
}() == 0b010);

TEST_CASE("nexenne::utility::flags compound assignment operators") {
  auto f{util::flags<perm>{}};

  f |= perm::read;
  f |= util::flags{perm::write};
  CHECK(f == rw);

  f &= perm::write;  // intersect down to a single bit
  CHECK(f == util::flags{perm::write});

  f ^= perm::write;  // symmetric-difference back to empty
  CHECK(f.none());

  f ^= util::flags{perm::exec};
  CHECK(f.has(perm::exec));
}

TEST_CASE("nexenne::utility::flags binary operators mix flags and enumerators") {
  // operator& and operator^ have no enum overload; the enumerator converts.
  CHECK((rw & perm::write) == util::flags{perm::write});
  CHECK((rw ^ perm::read) == util::flags{perm::write});
  CHECK((perm::read | util::flags{perm::exec}).raw() == 0b101);
  CHECK((util::flags{perm::read} | perm::exec).raw() == 0b101);
  // Idempotence / annihilation.
  CHECK((rw | rw) == rw);
  CHECK((rw & rw) == rw);
  CHECK((rw ^ rw).none());
}

TEST_CASE("nexenne::utility::flags empty-mask query semantics") {
  auto const empty_mask{static_cast<perm>(0)};
  // No bits requested: 'has all of nothing' is vacuously true, 'has any' false.
  CHECK(rw.has(empty_mask));
  CHECK(rw.has_all(empty_mask));
  CHECK_FALSE(rw.has_any(empty_mask));
  CHECK(util::flags<perm>{}.has(empty_mask));  // even an empty set has all of nothing
}

// A wide underlying type exercises far-apart bits and a full-width complement.
enum class wide : std::uint32_t {
  low = 1u << 0,
  mid = 1u << 15,
  high = 1u << 31
};

static_assert(util::flags<wide>::underlying_type{1} == std::uint32_t{1});
static_assert((util::flags{wide::low} | wide::high).raw() == (1u << 0 | 1u << 31));
static_assert((~util::flags{wide::low}).raw() == ~std::uint32_t{1});
static_assert(util::flags<wide>::from_raw(0xFFFFFFFFu).raw() == 0xFFFFFFFFu);

TEST_CASE("nexenne::utility::flags over a wide underlying type") {
  auto f{util::flags<wide>{}};
  f.set(wide::low).set(wide::high);
  CHECK(f.has(wide::low));
  CHECK(f.has(wide::high));
  CHECK_FALSE(f.has(wide::mid));
  CHECK(f.raw() == (1u << 0 | 1u << 31));

  // Round-trip the raw value through from_raw.
  CHECK(util::flags<wide>::from_raw(f.raw()) == f);

  // Complement sets every other bit, including mid.
  auto const inv{~f};
  CHECK(inv.has(wide::mid));
  CHECK_FALSE(inv.has(wide::low));
  CHECK(inv.raw() == ~f.raw());
}

}  // namespace
