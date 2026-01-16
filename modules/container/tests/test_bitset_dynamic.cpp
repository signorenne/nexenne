/**
 * @file
 * @brief Tests for nexenne::container::bitset_dynamic.
 */

#include <doctest/doctest.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

#include <nexenne/container/bitset_dynamic.hpp>

namespace {

namespace cn = nexenne::container;
using bs = cn::bitset_dynamic;

static_assert(std::forward_iterator<bs::set_bit_iterator>);

// bitset_dynamic is usable at compile time.
static_assert([] {
  bs b(10);
  b.set(3);
  b.set(7);
  return b.size() == 10 && b[3] && b[7] && !b[0] && b.count() == 2;
}());

TEST_CASE("nexenne::container::bitset_dynamic set/reset/flip/test and bounds") {
  bs b(8);
  CHECK(b.size() == 8);
  CHECK(b.count() == 0);
  CHECK(b.set(2).has_value());
  CHECK(b[2]);
  CHECK(*b.test(2));
  CHECK_FALSE(*b.test(3));
  CHECK(b.set(8).error() == cn::container_error::out_of_range);
  CHECK(b.test(8).error() == cn::container_error::out_of_range);
  CHECK(b.reset(2).has_value());
  CHECK_FALSE(b[2]);
  CHECK(b.flip(5).has_value());
  CHECK(b[5]);
}

TEST_CASE("nexenne::container::bitset_dynamic construct filled and from a list") {
  bs filled(3, true);
  CHECK(filled.count() == 3);
  CHECK(filled.all());

  bs list{true, false, true, true};
  CHECK(list.size() == 4);
  CHECK(list.count() == 3);
  CHECK(list[0]);
  CHECK_FALSE(list[1]);
  CHECK(list[2]);
}

TEST_CASE("nexenne::container::bitset_dynamic set_all/reset_all/flip_all and any/none/all") {
  bs b(100);
  CHECK(b.none());
  b.set_all();
  CHECK(b.all());
  CHECK(b.count() == 100);  // tail bits masked, not 128
  b.flip_all();
  CHECK(b.none());
  b.set(50);
  CHECK(b.any());
  b.reset_all();
  CHECK(b.none());
}

TEST_CASE("nexenne::container::bitset_dynamic count masks the tail (size not a multiple of 64)") {
  bs b(70);  // two words
  b.set_all();
  CHECK(b.count() == 70);
}

TEST_CASE("nexenne::container::bitset_dynamic find_first_set") {
  bs b(200);
  CHECK(b.find_first_set() == 200);  // none -> size
  b.set(130);
  b.set(5);
  CHECK(b.find_first_set() == 5);
  CHECK(b.find_first_set(6) == 130);
  CHECK(b.find_first_set(131) == 200);
}

TEST_CASE("nexenne::container::bitset_dynamic set_bits iterates indices sparsely") {
  bs b(300);
  b.set(1);
  b.set(64);
  b.set(200);
  std::vector<std::size_t> indices;
  for (auto const i : b.set_bits()) {
    indices.push_back(i);
  }
  CHECK(indices == std::vector<std::size_t>{1, 64, 200});

  std::vector<std::size_t> const via_begin(b.begin(), b.end());
  CHECK(via_begin == indices);
}

TEST_CASE("nexenne::container::bitset_dynamic bitwise operators") {
  bs const a{true, true, false, false};
  bs const b{true, false, true, false};

  auto const a_and_b{a & b};
  CHECK(a_and_b[0]);
  CHECK_FALSE(a_and_b[1]);
  CHECK_FALSE(a_and_b[2]);

  auto const a_or_b{a | b};
  CHECK(a_or_b[0]);
  CHECK(a_or_b[1]);
  CHECK(a_or_b[2]);
  CHECK_FALSE(a_or_b[3]);

  auto const a_xor_b{a ^ b};
  CHECK_FALSE(a_xor_b[0]);
  CHECK(a_xor_b[1]);
  CHECK(a_xor_b[2]);
}

TEST_CASE("nexenne::container::bitset_dynamic comparison") {
  CHECK(bs{true, false} == bs{true, false});
  CHECK(bs{true, false} != bs{true, true});
  CHECK((bs{false} <=> bs{true}) == std::strong_ordering::less);
}

TEST_CASE("nexenne::container::bitset_dynamic resize grow and shrink") {
  bs b(4, true);
  b.resize(8, true);  // grow, new bits set
  CHECK(b.size() == 8);
  CHECK(b.count() == 8);
  b.resize(2);  // shrink drops the rest
  CHECK(b.size() == 2);
  CHECK(b.count() == 2);
}

TEST_CASE("nexenne::container::bitset_dynamic push_back and clear") {
  bs b;
  b.push_back(true);
  b.push_back(false);
  b.push_back(true);
  CHECK(b.size() == 3);
  CHECK(b.count() == 2);
  CHECK(b[0]);
  CHECK(b[2]);
  b.clear();
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bitset_dynamic move zeros source; copy is independent; swap") {
  bs a(10);
  a.set(3);
  bs b{std::move(a)};
  CHECK(b.size() == 10);
  CHECK(b[3]);
  CHECK(a.empty());  // source zeroed

  bs c{b};
  CHECK(c == b);
  c.set(4);
  CHECK_FALSE(b[4]);  // deep copy

  bs d(5);
  swap(c, d);
  CHECK(d.size() == 10);
  CHECK(c.size() == 5);
}

TEST_CASE("nexenne::container::bitset_dynamic words exposes the raw storage") {
  bs b(64);
  b.set(0);
  b.set(63);
  auto const w{b.words()};
  CHECK(w.size() == 1);
  CHECK(w[0] == ((std::uint64_t{1} << 0) | (std::uint64_t{1} << 63)));
}

}  // namespace
