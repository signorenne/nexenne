/**
 * @file
 * @brief Tests for the nexenne::utility hash combiners.
 */

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

#include <nexenne/utility/hash.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(util::hashable<int>);
static_assert(util::hashable<std::string>);

struct not_hashable {};

static_assert(!util::hashable<not_hashable>);

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

TEST_CASE("nexenne::utility::hash_range hashes elements in order") {
  std::array const ascending{1, 2, 3};
  std::array const descending{3, 2, 1};

  CHECK(util::hash_range(ascending) == util::hash_range(std::array{1, 2, 3}));
  CHECK(util::hash_range(ascending) != util::hash_range(descending));
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

}  // namespace
