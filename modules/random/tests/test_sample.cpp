/**
 * @file
 * @brief Tests for nexenne::random sampling helpers.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <nexenne/random/pcg.hpp>
#include <nexenne/random/sample.hpp>

namespace {

namespace rnd = nexenne::random;

TEST_CASE("nexenne::random::shuffle permutes without losing or duplicating elements") {
  std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto const original{v};
  rnd::pcg32 g{1, 1};
  rnd::shuffle(v, g);
  CHECK(v != original);  // some permutation other than identity (overwhelmingly likely)
  std::ranges::sort(v);
  CHECK(v == original);  // a true permutation: same multiset
}

TEST_CASE("nexenne::random::reservoir_sample draws k distinct elements uniformly") {
  std::vector<int> population(100);
  std::ranges::generate(population, [n = 0]() mutable { return n++; });
  rnd::pcg32 g{2, 1};
  auto const picked{rnd::reservoir_sample(population, 10, g)};
  CHECK(picked.size() == 10);
  // every pick is from the population and they are distinct
  auto sorted{picked};
  std::ranges::sort(sorted);
  CHECK(std::ranges::adjacent_find(sorted) == sorted.end());  // no duplicates
  for (auto const x : picked) {
    CHECK(x >= 0);
    CHECK(x < 100);
  }
}

TEST_CASE("nexenne::random::reservoir_sample with k >= n returns the whole population") {
  std::vector<int> population{1, 2, 3};
  rnd::pcg32 g{3, 1};
  auto picked{rnd::reservoir_sample(population, 10, g)};
  std::ranges::sort(picked);
  CHECK(picked == std::vector{1, 2, 3});
}

TEST_CASE("nexenne::random::weighted_choice picks in proportion to weights") {
  std::array<double, 3> const weights{1.0, 0.0, 4.0};  // index 1 never, index 2 four times index 0
  rnd::pcg32 g{4, 1};
  std::array<int, 3> hits{};
  constexpr int n{100000};
  for (int i{0}; i < n; ++i) {
    auto const idx{rnd::weighted_choice(weights, g)};
    REQUIRE(idx < 3);
    ++hits[idx];
  }
  CHECK(hits[1] == 0);  // zero weight is never chosen
  CHECK(
    static_cast<double>(hits[2]) / static_cast<double>(hits[0] + 1)
    == doctest::Approx(4.0).epsilon(0.1)
  );
}

}  // namespace
