/**
 * @file
 * @brief Tests for nexenne::random sampling helpers.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
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

TEST_CASE("nexenne::random::shuffle leaves empty and single-element ranges untouched") {
  std::vector<int> empty{};
  rnd::pcg32 g{1, 1};
  rnd::shuffle(empty, g);
  CHECK(empty.empty());

  std::vector<int> one{42};
  rnd::shuffle(one, g);
  CHECK(one == std::vector{42});

  // A no-op must not consume engine output: an empty/single shuffle leaves the
  // engine state exactly where it started.
  rnd::pcg32 before{7, 1};
  rnd::pcg32 after{7, 1};
  std::vector<int> tiny{99};
  rnd::shuffle(tiny, after);
  std::vector<int> none{};
  rnd::shuffle(none, after);
  CHECK(before.state() == after.state());
}

TEST_CASE("nexenne::random::shuffle is deterministic for a fixed seed (regression baseline)") {
  // Two engines seeded identically must produce the identical permutation.
  std::vector<int> a{0, 1, 2, 3, 4, 5, 6, 7};
  std::vector<int> b{0, 1, 2, 3, 4, 5, 6, 7};
  rnd::pcg32 g1{123, 1};
  rnd::pcg32 g2{123, 1};
  rnd::shuffle(a, g1);
  rnd::shuffle(b, g2);
  CHECK(a == b);

  // Still a permutation of the original multiset.
  auto sorted{a};
  std::ranges::sort(sorted);
  CHECK(sorted == std::vector{0, 1, 2, 3, 4, 5, 6, 7});
}

TEST_CASE("nexenne::random::shuffle moves std::string elements without loss") {
  std::vector<std::string> words{
    "alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel"
  };
  auto const original{words};
  rnd::pcg32 g{55, 1};
  rnd::shuffle(words, g);
  std::ranges::sort(words);
  auto sorted_original{original};
  std::ranges::sort(sorted_original);
  CHECK(words == sorted_original);  // same multiset of strings, none lost/leaked
}

TEST_CASE("nexenne::random::shuffle spreads each element across all positions") {
  // Light uniformity probe: over many shuffles of a small array, no element
  // should be stuck in one position. Track how often value v lands at index i;
  // every (v, i) pair should be hit a non-trivial number of times.
  constexpr std::size_t size{5};
  constexpr int runs{40000};
  std::array<std::array<int, size>, size> counts{};  // counts[value][position]
  rnd::pcg32 g{9001, 1};
  for (int r{0}; r < runs; ++r) {
    std::array<int, size> v{0, 1, 2, 3, 4};
    rnd::shuffle(v, g);
    for (std::size_t pos{0}; pos < size; ++pos) {
      ++counts[static_cast<std::size_t>(v[pos])][pos];
    }
  }
  auto const expected{static_cast<double>(runs) / static_cast<double>(size)};
  for (std::size_t value{0}; value < size; ++value) {
    for (std::size_t pos{0}; pos < size; ++pos) {
      // Each value lands in each position ~runs/size times; allow generous slack.
      CHECK(static_cast<double>(counts[value][pos]) == doctest::Approx(expected).epsilon(0.1));
    }
  }
}

TEST_CASE("nexenne::random::reservoir_sample handles edge values of k") {
  std::vector<int> population{1, 2, 3, 4, 5};
  rnd::pcg32 g{11, 1};

  SUBCASE("k == 0 returns nothing") {
    auto const picked{rnd::reservoir_sample(population, 0, g)};
    CHECK(picked.empty());
  }

  SUBCASE("k == 1 returns exactly one element from the input") {
    auto const picked{rnd::reservoir_sample(population, 1, g)};
    CHECK(picked.size() == 1);
    CHECK(std::ranges::find(population, picked.front()) != population.end());
  }

  SUBCASE("k == n returns the whole population") {
    auto picked{rnd::reservoir_sample(population, population.size(), g)};
    CHECK(picked.size() == population.size());
    std::ranges::sort(picked);
    CHECK(picked == std::vector{1, 2, 3, 4, 5});
  }

  SUBCASE("empty input yields an empty sample for any k") {
    std::vector<int> empty{};
    auto const picked{rnd::reservoir_sample(empty, 4, g)};
    CHECK(picked.empty());
  }
}

TEST_CASE("nexenne::random::reservoir_sample is reproducible for a fixed seed") {
  std::vector<int> population(50);
  std::ranges::generate(population, [n = 0]() mutable { return n++; });
  rnd::pcg32 g1{77, 1};
  rnd::pcg32 g2{77, 1};
  auto const a{rnd::reservoir_sample(population, 12, g1)};
  auto const b{rnd::reservoir_sample(population, 12, g2)};
  CHECK(a == b);  // same seed, same draw order, identical result
}

TEST_CASE("nexenne::random::reservoir_sample selects every element with ~uniform probability") {
  // Headline correctness property of Algorithm R: across many independent
  // samples, each of the n input elements is retained with probability k/n.
  constexpr std::size_t n{20};
  constexpr std::size_t k{5};
  constexpr int runs{60000};
  std::vector<int> population(n);
  std::ranges::generate(population, [v = 0]() mutable { return v++; });

  std::array<int, n> selected{};  // selected[v] = how many runs retained value v
  rnd::pcg32 g{4242, 1};
  for (int r{0}; r < runs; ++r) {
    auto const picked{rnd::reservoir_sample(population, k, g)};
    REQUIRE(picked.size() == k);
    for (auto const v : picked) {
      ++selected[static_cast<std::size_t>(v)];
    }
  }

  auto const expected{static_cast<double>(runs) * static_cast<double>(k) / static_cast<double>(n)};
  for (std::size_t v{0}; v < n; ++v) {
    CHECK(static_cast<double>(selected[v]) == doctest::Approx(expected).epsilon(0.07));
  }
}

TEST_CASE("nexenne::random::reservoir_sample preserves std::string elements from the input") {
  std::vector<std::string> population{
    "red", "orange", "yellow", "green", "blue", "indigo", "violet"
  };
  rnd::pcg32 g{321, 1};
  auto const picked{rnd::reservoir_sample(population, 3, g)};
  CHECK(picked.size() == 3);
  for (auto const& s : picked) {
    CHECK(std::ranges::find(population, s) != population.end());  // came from the input
  }
}

TEST_CASE("nexenne::random::weighted_choice returns the sentinel for empty or all-zero weights") {
  rnd::pcg32 g{13, 1};

  SUBCASE("empty weights") {
    std::span<double const> const empty{};
    CHECK(rnd::weighted_choice(empty, g) == 0u);  // weights.size() == 0
  }

  SUBCASE("all weights zero") {
    std::array<double, 4> const zeros{0.0, 0.0, 0.0, 0.0};
    CHECK(rnd::weighted_choice(zeros, g) == zeros.size());
  }

  SUBCASE("all weights negative are treated as zero -> sentinel") {
    std::array<double, 3> const negs{-1.0, -2.0, -0.5};
    CHECK(rnd::weighted_choice(negs, g) == negs.size());
  }
}

TEST_CASE("nexenne::random::weighted_choice always returns the only positive index") {
  // A single non-zero weight (surrounded by zeros/negatives) must be the
  // unique outcome on every draw.
  std::array<double, 4> const weights{0.0, -3.0, 7.5, 0.0};
  rnd::pcg32 g{14, 1};
  for (int i{0}; i < 1000; ++i) {
    CHECK(rnd::weighted_choice(weights, g) == 2u);
  }
}

TEST_CASE("nexenne::random::weighted_choice is reproducible for a fixed seed") {
  std::array<double, 5> const weights{1.0, 2.0, 3.0, 4.0, 5.0};
  rnd::pcg32 g1{15, 1};
  rnd::pcg32 g2{15, 1};
  std::vector<std::size_t> a;
  std::vector<std::size_t> b;
  for (int i{0}; i < 200; ++i) {
    a.push_back(rnd::weighted_choice(weights, g1));
    b.push_back(rnd::weighted_choice(weights, g2));
  }
  CHECK(a == b);
}

TEST_CASE("nexenne::random::weighted_choice matches normalized weights via chi-square") {
  // Empirical selection frequency over a large N should match the normalized
  // weight of every positive index; zero/negative indices stay unselected.
  std::array<double, 5> const weights{0.5, 0.0, 1.5, -2.0, 3.0};  // negatives count as zero
  auto total{0.0};
  for (auto const w : weights) {
    if (w > 0.0) {
      total += w;
    }
  }

  constexpr int n{200000};
  std::array<int, 5> hits{};
  rnd::pcg32 g{16, 1};
  for (int i{0}; i < n; ++i) {
    auto const idx{rnd::weighted_choice(weights, g)};
    REQUIRE(idx < weights.size());
    ++hits[idx];
  }

  CHECK(hits[1] == 0);  // zero weight never chosen
  CHECK(hits[3] == 0);  // negative weight never chosen

  // Pearson chi-square against the expected counts of the positive indices.
  auto chi_square{0.0};
  for (std::size_t i{0}; i < weights.size(); ++i) {
    if (weights[i] <= 0.0) {
      continue;
    }
    auto const expected{static_cast<double>(n) * weights[i] / total};
    auto const diff{static_cast<double>(hits[i]) - expected};
    chi_square += diff * diff / expected;
    // Per-item tolerance as a second, independent check.
    CHECK(static_cast<double>(hits[i]) == doctest::Approx(expected).epsilon(0.03));
  }
  // 3 positive categories -> 2 degrees of freedom; 13.8 ~ p=0.001 critical value.
  CHECK(chi_square < 13.8);
}

TEST_CASE("nexenne::random::weighted_choice with all-equal weights is ~uniform") {
  constexpr std::size_t categories{6};
  std::array<double, categories> const weights{1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  constexpr int n{120000};
  std::array<int, categories> hits{};
  rnd::pcg32 g{17, 1};
  for (int i{0}; i < n; ++i) {
    auto const idx{rnd::weighted_choice(weights, g)};
    REQUIRE(idx < categories);
    ++hits[idx];
  }
  auto const expected{static_cast<double>(n) / static_cast<double>(categories)};
  for (std::size_t i{0}; i < categories; ++i) {
    CHECK(static_cast<double>(hits[i]) == doctest::Approx(expected).epsilon(0.05));
  }
}

}  // namespace
