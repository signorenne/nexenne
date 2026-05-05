/**
 * @file
 * @brief Tests for nexenne::utility::flags.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <type_traits>

#include <nexenne/utility/flags.hpp>

namespace {

namespace util = nexenne::utility;

enum class perm : std::uint8_t {
  read = 1 << 0,
  write = 1 << 1,
  exec = 1 << 2
};

static_assert(std::is_same_v<util::flags<perm>::value_type, perm>);
static_assert(std::is_same_v<util::flags<perm>::enum_type, perm>);
static_assert(std::is_same_v<util::flags<perm>::unsigned_type, std::uint8_t>);

static_assert(util::flags<perm>{}.none());
static_assert(util::flags{perm::read}.has(perm::read));
static_assert(!util::flags{perm::read}.has(perm::write));

// Boolean test and popcount.
static_assert(!static_cast<bool>(util::flags<perm>{}));
static_assert(static_cast<bool>(util::flags{perm::read}));
static_assert(util::flags<perm>{}.count() == 0);
static_assert(util::flags{perm::read}.count() == 1);

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
  // The full symmetric overload set: | & ^ each accept an enumerator on
  // either side and return a flags.
  CHECK((rw & perm::write) == util::flags{perm::write});
  CHECK((perm::write & rw) == util::flags{perm::write});
  CHECK((rw ^ perm::read) == util::flags{perm::write});
  CHECK((perm::read ^ rw) == util::flags{perm::write});
  CHECK((perm::read | util::flags{perm::exec}).raw() == 0b101);
  CHECK((util::flags{perm::read} | perm::exec).raw() == 0b101);
  // Idempotence / annihilation.
  CHECK((rw | rw) == rw);
  CHECK((rw & rw) == rw);
  CHECK((rw ^ rw).none());
}

// The enum-operand binary operators are constexpr and symmetric.
static_assert((rw & perm::read) == util::flags{perm::read});
static_assert((perm::read & rw) == util::flags{perm::read});
static_assert((rw ^ perm::write) == util::flags{perm::read});
static_assert((perm::write ^ rw) == util::flags{perm::read});

TEST_CASE("nexenne::utility::flags converts to bool and counts set bits") {
  auto f{util::flags<perm>{}};
  CHECK_FALSE(static_cast<bool>(f));
  CHECK(f.count() == 0);

  f.set(perm::read).set(perm::exec);
  CHECK(static_cast<bool>(f));  // explicit: usable directly in an if condition
  if (f) {
    CHECK(f.count() == 2);
  }

  f.clear_all();
  CHECK_FALSE(static_cast<bool>(f));
}

TEST_CASE("nexenne::utility::flags formats its raw mask through std::format") {
  // The formatter prints the unsigned raw value; specs pass through to the
  // integer formatter.
  CHECK(std::format("{}", rw) == "3");
  CHECK(std::format("{:#05b}", rw) == "0b011");
  CHECK(std::format("{:#x}", util::flags{perm::exec}) == "0x4");
  CHECK(std::format("{}", util::flags<perm>{}) == "0");
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

// A bare scoped enum defaults to a SIGNED int underlying type; count() and the
// formatter must go through the unsigned counterpart, never the sign.
enum class option {
  alpha = 1 << 0,
  beta = 1 << 1,
  gamma = 1 << 2
};

static_assert(std::is_signed_v<util::flags<option>::underlying_type>);
static_assert(std::is_same_v<util::flags<option>::unsigned_type, unsigned int>);

TEST_CASE("nexenne::utility::flags over a signed underlying type") {
  auto f{util::flags{option::alpha} | option::gamma};
  CHECK(f.has(option::alpha));
  CHECK(f.has(option::gamma));
  CHECK(f.count() == 2);

  f.clear(option::gamma);
  CHECK_FALSE(f.has(option::gamma));
  CHECK(f.count() == 1);

  // The complement of an empty set is raw ~0, negative as a signed int; the
  // bit count and the formatting still see the two's-complement pattern.
  auto const all{~util::flags<option>{}};
  CHECK(all.raw() == -1);
  CHECK(all.count() == std::size_t{32});  // every bit of the 32-bit underlying int
  CHECK(all.has(option::beta));
  CHECK(std::format("{:#x}", all) == "0xffffffff");

  CHECK(std::format("{}", f) == "1");
  CHECK(std::format("{:#05b}", util::flags{option::beta} | option::alpha) == "0b011");
}

}  // namespace
