/**
 * @file
 * @brief Tests for the nexenne::utility hash combiners.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <list>
#include <string>
#include <vector>

#include <nexenne/utility/hash.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(util::hashable<int>);
static_assert(util::hashable<std::string>);
static_assert(util::hashable<std::size_t>);
static_assert(util::hashable<char const*>);

struct not_hashable {};

static_assert(!util::hashable<not_hashable>);

// The width-tuned mixer only admits a 4- or 8-byte std::size_t; the active
// specialisation must expose its constants.
static_assert(util::detail::hash_mix<>::magic != 0);
static_assert(sizeof(std::size_t) == 4 || sizeof(std::size_t) == 8);

TEST_CASE("nexenne::utility::hash_args is deterministic and order-sensitive") {
  auto const same_a{util::hash_args(1, 2, 3)};
  auto const same_b{util::hash_args(1, 2, 3)};
  auto const reordered{util::hash_args(3, 2, 1)};

  CHECK(same_a == same_b);
  CHECK(same_a != reordered);
}

TEST_CASE("nexenne::utility::hash_args mixes heterogeneous values") {
  auto const h{util::hash_args(42, std::string{"x"}, 3.5)};
  CHECK(h != 0);
}

TEST_CASE("nexenne::utility::hash_args of a single value matches hashing it directly") {
  // hash_args(x) == hash_combine(0, x), the documented one-value behaviour.
  std::size_t seed{0};
  util::hash_combine(seed, 12345);
  CHECK(util::hash_args(12345) == seed);
}

TEST_CASE("nexenne::utility::hash_combine folds into an existing seed in order") {
  std::size_t a{0};
  util::hash_combine(a, 1);
  util::hash_combine(a, 2);

  std::size_t b{0};
  util::hash_combine_each(b, 1, 2);

  CHECK(a == b);  // manual two-step fold equals hash_combine_each
  CHECK(a == util::hash_args(1, 2));
}

TEST_CASE("nexenne::utility::hash_combine_each matches a manual left fold") {
  std::size_t seed{99};  // non-zero starting accumulator
  util::hash_combine_each(seed, 7, 8, 9);

  std::size_t manual{99};
  util::hash_combine(manual, 7);
  util::hash_combine(manual, 8);
  util::hash_combine(manual, 9);

  CHECK(seed == manual);
}

TEST_CASE("nexenne::utility::hash_combine_each on an empty pack is the identity") {
  std::size_t seed{0xABCD};
  util::hash_combine_each(seed);
  CHECK(seed == 0xABCD);  // unchanged
}

TEST_CASE("nexenne::utility::hash_combine is order-sensitive for two values") {
  CHECK(util::hash_args(1, 2) != util::hash_args(2, 1));
}

TEST_CASE("nexenne::utility::hash_range hashes elements in order") {
  std::array const ascending{1, 2, 3};
  std::array const descending{3, 2, 1};

  CHECK(util::hash_range(ascending) == util::hash_range(std::array{1, 2, 3}));
  CHECK(util::hash_range(ascending) != util::hash_range(descending));
}

TEST_CASE("nexenne::utility::hash_range matches an equivalent hash_args fold") {
  std::vector const v{10, 20, 30};
  CHECK(util::hash_range(v) == util::hash_args(10, 20, 30));
}

TEST_CASE("nexenne::utility::hash_range is independent of the container type") {
  std::vector const v{4, 5, 6};
  std::list const l{4, 5, 6};
  std::array const a{4, 5, 6};
  CHECK(util::hash_range(v) == util::hash_range(l));
  CHECK(util::hash_range(v) == util::hash_range(a));
}

TEST_CASE("nexenne::utility::hash_range over strings is order-sensitive") {
  std::vector<std::string> const fwd{"a", "b", "c"};
  std::vector<std::string> const rev{"c", "b", "a"};
  CHECK(util::hash_range(fwd) != util::hash_range(rev));
  CHECK(util::hash_range(fwd) == util::hash_range(std::vector<std::string>{"a", "b", "c"}));
}

TEST_CASE("nexenne::utility::hash of empty input is zero") {
  CHECK(util::hash_args() == 0);
  CHECK(util::hash_range(std::vector<int>{}) == 0);

  std::size_t seed{0};
  util::hash_combine_each(seed);  // empty pack leaves the seed unchanged
  CHECK(seed == 0);
}

TEST_CASE("nexenne::utility::hash_range reflects element multiplicity") {
  CHECK(util::hash_range(std::array{1, 1}) != util::hash_range(std::array{1}));
}

TEST_CASE("nexenne::utility::hash_range of a single element equals a single combine") {
  CHECK(util::hash_range(std::array{42}) == util::hash_args(42));
}

TEST_CASE("nexenne::utility distinct inputs differ in practice") {
  CHECK(util::hash_args(0) != util::hash_args(1));
  CHECK(util::hash_args(1, 0) != util::hash_args(0, 1));
  CHECK(util::hash_args(std::string{"abc"}) != util::hash_args(std::string{"abd"}));
  // Zero-seed combine of a zero hash is still nonzero (the magic constant mixes in).
  std::size_t seed{0};
  util::hash_combine(seed, std::size_t{0});
  CHECK(seed != 0);
}

}  // namespace
