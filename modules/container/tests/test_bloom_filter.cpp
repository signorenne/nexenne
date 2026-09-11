/**
 * @file
 * @brief Tests for nexenne::container::bloom_filter.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <type_traits>

#include <nexenne/container/bloom_filter.hpp>
#include <nexenne/container/error.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::bloom_filter never reports a false negative") {
  auto f{cn::bloom_filter<int>::make(1024, 4).value()};
  for (int i{0}; i < 200; ++i) {
    f.insert(i);
  }
  for (int i{0}; i < 200; ++i) {
    CHECK(f.contains(i));  // every inserted value must be reported present
  }
  CHECK(f.insertions() == 200);
  CHECK_FALSE(f.empty());
}

TEST_CASE("nexenne::container::bloom_filter reports clear absences") {
  auto f{cn::bloom_filter<std::string>::make(4096, 5).value()};
  f.insert("alpha");
  f.insert("beta");
  CHECK(f.contains("alpha"));
  CHECK(f.contains("beta"));
  // With a large, lightly loaded filter these are almost certainly absent.
  CHECK_FALSE(f.contains("gamma"));
  CHECK_FALSE(f.contains("delta"));
}

TEST_CASE("nexenne::container::bloom_filter handles a value whose hash is zero") {
  // std::hash<int>(0) is 0 on libstdc++; the odd-h2 fix must keep the k bit
  // positions distinct so 0 does not collapse onto a single shared bit.
  auto f{cn::bloom_filter<int>::make(64, 4).value()};
  f.insert(0);
  CHECK(f.contains(0));
  // 0 must not make every other value look present: most should be absent.
  int present{0};
  for (int i{1}; i < 40; ++i) {
    if (f.contains(i)) {
      ++present;
    }
  }
  CHECK(present < 20);  // far from "everything matches bit 0"
}

TEST_CASE("nexenne::container::bloom_filter sizing factory hits roughly the target rate") {
  auto f{cn::bloom_filter<int>::with_target_false_positive_rate(1000, 0.01).value()};
  CHECK(f.bit_count() > 1000);  // ~9.6 bits/item
  CHECK(f.hash_count() >= 1);
  for (int i{0}; i < 1000; ++i) {
    f.insert(i);
  }
  for (int i{0}; i < 1000; ++i) {
    CHECK(f.contains(i));  // no false negatives at the design load
  }
  CHECK(f.false_positive_rate() > 0.0);
  CHECK(f.false_positive_rate() < 0.05);  // near the 1% target, generous bound
}

TEST_CASE("nexenne::container::bloom_filter clear resets all bits") {
  auto f{cn::bloom_filter<int>::make(256, 3).value()};
  f.insert(42);
  CHECK(f.contains(42));
  f.clear();
  CHECK(f.empty());
  CHECK_FALSE(f.contains(42));
  CHECK(f.bit_count() == 256);  // shape kept
  CHECK(f.hash_count() == 3);
}

TEST_CASE("nexenne::container::bloom_filter merge unions two same-shaped filters") {
  auto a{cn::bloom_filter<int>::make(512, 4).value()};
  auto b{cn::bloom_filter<int>::make(512, 4).value()};
  a.insert(1);
  a.insert(2);
  b.insert(3);
  REQUIRE(a.merge(b).has_value());
  CHECK(a.contains(1));
  CHECK(a.contains(2));
  CHECK(a.contains(3));  // b's element now present in a

  auto mismatch{cn::bloom_filter<int>::make(256, 4).value()};
  CHECK(a.merge(mismatch).error() == cn::container_error::invalid_argument);
}

TEST_CASE("nexenne::container::bloom_filter swap and equality") {
  auto a{cn::bloom_filter<int>::make(128, 3).value()};
  a.insert(7);
  auto b{cn::bloom_filter<int>::make(128, 3).value()};
  b.insert(7);
  CHECK(a == b);  // same bits and hash count
  auto c{cn::bloom_filter<int>::make(128, 3).value()};
  c.insert(8);
  CHECK_FALSE(a == c);

  auto d{cn::bloom_filter<int>::make(128, 3).value()};
  swap(a, d);
  CHECK(d.contains(7));
  CHECK(a.empty());
}

TEST_CASE("nexenne::container::bloom_filter empty filter has zero false-positive rate") {
  auto f{cn::bloom_filter<int>::make(256, 4).value()};
  CHECK(f.empty());
  CHECK(f.insertions() == 0);
  CHECK(f.false_positive_rate() == 0.0);  // nothing inserted -> no positives
  CHECK_FALSE(f.contains(123));
}

TEST_CASE("nexenne::container::bloom_filter single-bit single-hash degenerate filter") {
  // The smallest legal shape: one bit, one hash. Every value maps to bit 0, so
  // after one insert everything tests present, yet no false negatives ever and
  // the modulo-by-size path stays well-defined (the div-by-zero guard requires
  // size >= 1, which this exercises at the boundary).
  auto f{cn::bloom_filter<int>::make(1, 1).value()};
  CHECK(f.bit_count() == 1);
  CHECK(f.hash_count() == 1);
  f.insert(42);
  CHECK(f.contains(42));  // no false negative
  CHECK(f.contains(99));  // saturated: a false positive, as expected
  CHECK(f.false_positive_rate() > 0.0);
}

TEST_CASE(
  "nexenne::container::bloom_filter no false negatives across many hash-zero-prone values"
) {
  // Sweep values including 0 and small ints whose splitmix64 second hash could be
  // even before the odd-forcing fix; every inserted value must still be present.
  auto f{cn::bloom_filter<int>::make(2048, 7).value()};
  for (int i{-100}; i <= 100; ++i) {
    f.insert(i);
  }
  for (int i{-100}; i <= 100; ++i) {
    CHECK(f.contains(i));
  }
  CHECK(f.insertions() == 201);
}

TEST_CASE(
  "nexenne::container::bloom_filter false-positive rate stays within bound over many items"
) {
  // Insert the design load, then probe a disjoint key range and count positives.
  auto f{cn::bloom_filter<std::uint64_t>::with_target_false_positive_rate(2000, 0.01).value()};
  for (std::uint64_t i{0}; i < 2000; ++i) {
    f.insert(i);
  }
  // None of the inserted may be missing.
  for (std::uint64_t i{0}; i < 2000; ++i) {
    CHECK(f.contains(i));
  }
  // Probe 10000 definitely-absent keys; observed FPR should be near 1%.
  std::size_t positives{0};
  std::size_t const probes{10000};
  for (std::uint64_t i{1'000'000}; i < 1'000'000 + probes; ++i) {
    if (f.contains(i)) {
      ++positives;
    }
  }
  double const observed{static_cast<double>(positives) / static_cast<double>(probes)};
  CHECK(observed < 0.05);  // generous ceiling around the 1% design target
}

TEST_CASE("nexenne::container::bloom_filter of std::string never reports false negatives") {
  auto f{cn::bloom_filter<std::string>::make(8192, 6).value()};
  std::mt19937 rng{99};
  std::uniform_int_distribution<int> ch{'a', 'z'};
  std::vector<std::string> inserted;
  for (int i{0}; i < 300; ++i) {
    std::string s;
    for (int j{0}; j < 8; ++j) {
      s.push_back(static_cast<char>(ch(rng)));
    }
    inserted.push_back(s);
    f.insert(s);
  }
  for (auto const& s : inserted) {
    CHECK(f.contains(s));  // every inserted string is present under LSan
  }
}

TEST_CASE("nexenne::container::bloom_filter merge accumulates the insertion counter") {
  auto a{cn::bloom_filter<int>::make(512, 4).value()};
  auto b{cn::bloom_filter<int>::make(512, 4).value()};
  a.insert(1);
  a.insert(2);
  b.insert(3);
  b.insert(4);
  b.insert(5);
  REQUIRE(a.merge(b).has_value());
  CHECK(a.insertions() == 5);  // 2 + 3
}

TEST_CASE("nexenne::container::bloom_filter merge rejects a differing hash count") {
  auto a{cn::bloom_filter<int>::make(512, 4).value()};
  auto b{cn::bloom_filter<int>::make(512, 5).value()};
  a.insert(1);
  CHECK(a.merge(b).error() == cn::container_error::invalid_argument);
  CHECK(a.insertions() == 1);  // unchanged on failure
}

TEST_CASE("nexenne::container::bloom_filter factory clamps tiny inputs to at least one hash") {
  // Aggressive (loose) target on a single item: k is floored at 1, m >= 1.
  auto f{cn::bloom_filter<int>::with_target_false_positive_rate(1, 0.99).value()};
  CHECK(f.hash_count() >= 1);
  CHECK(f.bit_count() >= 1);
  f.insert(5);
  CHECK(f.contains(5));  // still no false negatives at the degenerate size
}

TEST_CASE("nexenne::container::bloom_filter factory is well-formed across its domain") {
  for (auto const items : {std::size_t{1}, std::size_t{16}, std::size_t{1000}}) {
    for (auto const rate : {0.5, 0.1, 0.01, 0.001}) {
      auto const f{cn::bloom_filter<int>::with_target_false_positive_rate(items, rate).value()};
      CHECK(f.bit_count() > 0);
      CHECK(f.hash_count() >= 1);
      CHECK(f.empty());
    }
  }
}

TEST_CASE("nexenne::container::bloom_filter make rejects zero bits or hashes (container-27)") {
  // Checked in every build: zero bits would divide by zero mapping a hash to a
  // bit, and zero hashes would report every value present.
  CHECK(cn::bloom_filter<int>::make(0, 4).error() == cn::container_error::invalid_argument);
  CHECK(cn::bloom_filter<int>::make(64, 0).error() == cn::container_error::invalid_argument);
  CHECK(cn::bloom_filter<int>::make(0, 0).error() == cn::container_error::invalid_argument);
  auto const ok{cn::bloom_filter<int>::make(64, 3)};
  REQUIRE(ok.has_value());
  CHECK(ok->bit_count() == 64);
  CHECK(ok->hash_count() == 3);
  CHECK(ok->empty());
  CHECK_FALSE(ok->contains(1));
  static_assert(!std::is_constructible_v<cn::bloom_filter<int>, std::size_t, std::size_t>);
}

TEST_CASE("nexenne::container::bloom_filter sizing factory rejects bad inputs") {
  using filter = cn::bloom_filter<int>;
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  for (auto const rate : {0.0, 1.0, 1.5, -0.01, nan}) {
    auto const f{filter::with_target_false_positive_rate(100, rate)};
    REQUIRE_FALSE(f.has_value());
    CHECK(f.error() == cn::container_error::invalid_argument);
  }
  CHECK(
    filter::with_target_false_positive_rate(0, 0.01).error()
    == cn::container_error::invalid_argument
  );
  auto const huge{std::numeric_limits<std::size_t>::max()};
  CHECK(
    filter::with_target_false_positive_rate(huge, 1e-300).error()
    == cn::container_error::invalid_argument
  );
}

}  // namespace
