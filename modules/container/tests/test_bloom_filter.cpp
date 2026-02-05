/**
 * @file
 * @brief Tests for nexenne::container::bloom_filter.
 */

#include <doctest/doctest.h>

#include <string>

#include <nexenne/container/bloom_filter.hpp>
#include <nexenne/container/error.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::bloom_filter never reports a false negative") {
  cn::bloom_filter<int> f{1024, 4};
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
  cn::bloom_filter<std::string> f{4096, 5};
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
  cn::bloom_filter<int> f{64, 4};
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
  auto f{cn::bloom_filter<int>::with_target_false_positive_rate(1000, 0.01)};
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
  cn::bloom_filter<int> f{256, 3};
  f.insert(42);
  CHECK(f.contains(42));
  f.clear();
  CHECK(f.empty());
  CHECK_FALSE(f.contains(42));
  CHECK(f.bit_count() == 256);  // shape kept
  CHECK(f.hash_count() == 3);
}

TEST_CASE("nexenne::container::bloom_filter merge unions two same-shaped filters") {
  cn::bloom_filter<int> a{512, 4};
  cn::bloom_filter<int> b{512, 4};
  a.insert(1);
  a.insert(2);
  b.insert(3);
  REQUIRE(a.merge(b).has_value());
  CHECK(a.contains(1));
  CHECK(a.contains(2));
  CHECK(a.contains(3));  // b's element now present in a

  cn::bloom_filter<int> mismatch{256, 4};
  CHECK(a.merge(mismatch).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::bloom_filter swap and equality") {
  cn::bloom_filter<int> a{128, 3};
  a.insert(7);
  cn::bloom_filter<int> b{128, 3};
  b.insert(7);
  CHECK(a == b);  // same bits and hash count
  cn::bloom_filter<int> c{128, 3};
  c.insert(8);
  CHECK_FALSE(a == c);

  cn::bloom_filter<int> d{128, 3};
  swap(a, d);
  CHECK(d.contains(7));
  CHECK(a.empty());
}

}  // namespace
