/**
 * @file
 * @brief Tests for nexenne::container::bitset_dynamic.
 */

#include <doctest/doctest.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <nexenne/container/bitset_dynamic.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace cn = nexenne::container;
using bs = cn::bitset_dynamic;

static_assert(std::forward_iterator<bs::set_bit_iterator>);

static_assert([] {
  bs b(10);
  nexenne::utility::ignore(b.set(3));
  nexenne::utility::ignore(b.set(7));
  return b.size() == 10 && b[3] && b[7] && !b[0] && b.count() == 2;
}());

static_assert([] {
  bs const b;
  return b.empty() && b.size() == 0 && b.word_size() == 0 && b.none() && b.all();
}());

static_assert([] {
  bs b(65);
  b.set_all();
  return b.count() == 65 && b.all() && b.word_size() == 2;
}());

static_assert([] {
  bs b(70);
  b.flip_all();
  return b.count() == 70 && b.all();
}());

static_assert([] {
  bs b(4);
  return !b.set(4).has_value() && b.set(4).error() == cn::container_error::out_of_range
         && !b.test(9).has_value();
}());

static_assert([] {
  bs b(130);
  nexenne::utility::ignore(b.set(129));
  auto const first{b.find_first_set()};
  bs moved{std::move(b)};
  return first == 129 && moved[129] && moved.find_first_set(0) == 129
         && moved.find_first_set(130) == 130;
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
  CHECK(b.count() == 100);
  b.flip_all();
  CHECK(b.none());
  nexenne::utility::ignore(b.set(50));
  CHECK(b.any());
  b.reset_all();
  CHECK(b.none());
}

TEST_CASE("nexenne::container::bitset_dynamic count masks the tail (size not a multiple of 64)") {
  bs b(70);
  b.set_all();
  CHECK(b.count() == 70);
}

TEST_CASE("nexenne::container::bitset_dynamic find_first_set") {
  bs b(200);
  CHECK(b.find_first_set() == 200);
  nexenne::utility::ignore(b.set(130));
  nexenne::utility::ignore(b.set(5));
  CHECK(b.find_first_set() == 5);
  CHECK(b.find_first_set(6) == 130);
  CHECK(b.find_first_set(131) == 200);
}

TEST_CASE("nexenne::container::bitset_dynamic set_bits iterates indices sparsely") {
  bs b(300);
  nexenne::utility::ignore(b.set(1));
  nexenne::utility::ignore(b.set(64));
  nexenne::utility::ignore(b.set(200));
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
  b.resize(8, true);
  CHECK(b.size() == 8);
  CHECK(b.count() == 8);
  b.resize(2);
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
  nexenne::utility::ignore(a.set(3));
  bs b{std::move(a)};
  CHECK(b.size() == 10);
  CHECK(b[3]);
  CHECK(a.empty());

  bs c{b};
  CHECK(c == b);
  nexenne::utility::ignore(c.set(4));
  CHECK_FALSE(b[4]);

  bs d(5);
  swap(c, d);
  CHECK(d.size() == 10);
  CHECK(c.size() == 5);
}

TEST_CASE("nexenne::container::bitset_dynamic words exposes the raw storage") {
  bs b(64);
  nexenne::utility::ignore(b.set(0));
  nexenne::utility::ignore(b.set(63));
  auto const w{b.words()};
  CHECK(w.size() == 1);
  CHECK(w[0] == ((std::uint64_t{1} << 0) | (std::uint64_t{1} << 63)));
}

TEST_CASE("nexenne::container::bitset_dynamic mixed-width ops, multi-word resize, word boundary") {
  bs wide(100);
  wide.set_all();
  bs narrow(10);
  narrow.set_all();
  wide &= narrow;
  CHECK(wide.count() == 10);

  bs grow(60, true);
  grow.resize(130, true);
  CHECK(grow.size() == 130);
  CHECK(grow.count() == 130);

  bs pb;
  for (int i{0}; i < 65; ++i) {
    pb.push_back(true);
  }
  CHECK(pb.word_size() == 2);
  CHECK(pb[64]);
  CHECK(pb.count() == 65);
}

TEST_CASE("nexenne::container::bitset_dynamic empty bitset queries are well defined") {
  bs const b;
  CHECK(b.empty());
  CHECK(b.size() == 0);
  CHECK(b.word_size() == 0);
  CHECK(b.count() == 0);
  CHECK_FALSE(b.any());
  CHECK(b.none());
  CHECK(b.all());
  CHECK(b.find_first_set() == 0);
  CHECK(b.find_first_set(5) == 0);
  CHECK(b.words().empty());
  CHECK(b.begin() == b.end());
  bs mb;
  CHECK(mb.set(0).error() == cn::container_error::out_of_range);
  CHECK(mb.test(0).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::bitset_dynamic out-of-range error path on every checked op") {
  bs b(8);
  CHECK(b.set(8).error() == cn::container_error::out_of_range);
  CHECK(b.reset(8).error() == cn::container_error::out_of_range);
  CHECK(b.flip(8).error() == cn::container_error::out_of_range);
  CHECK(b.test(8).error() == cn::container_error::out_of_range);
  CHECK(b.set(1000).error() == cn::container_error::out_of_range);
  CHECK(b.none());
  CHECK(b.set(7).has_value());
  auto const before{b};
  CHECK(b.reset(8).error() == cn::container_error::out_of_range);
  CHECK(b == before);
  CHECK(b.set(7).has_value());
  CHECK(b.flip(7).has_value());
  CHECK_FALSE(b[7]);
}

TEST_CASE("nexenne::container::bitset_dynamic word-boundary sizes 63/64/65 mask the tail") {
  for (auto const n : {std::size_t{63}, std::size_t{64}, std::size_t{65}}) {
    bs b(n);
    CHECK(b.none());
    CHECK(b.count() == 0);
    CHECK_FALSE(b.all());
    b.set_all();
    CHECK(b.all());
    CHECK(b.any());
    CHECK(b.count() == n);
    b.flip_all();
    CHECK(b.none());
    CHECK(b.count() == 0);
  }

  CHECK(bs(64).word_size() == 1);
  CHECK(bs(65).word_size() == 2);
  CHECK(bs(63).word_size() == 1);
}

TEST_CASE("nexenne::container::bitset_dynamic word-boundary sizes 127/128/129 mask the tail") {
  for (auto const n : {std::size_t{127}, std::size_t{128}, std::size_t{129}}) {
    bs b(n, true);
    CHECK(b.all());
    CHECK(b.count() == n);
    b.flip_all();
    CHECK(b.none());
    CHECK(b.count() == 0);
    b.set_all();
    CHECK(b.count() == n);
  }

  CHECK(bs(128).word_size() == 2);
  CHECK(bs(129).word_size() == 3);
  CHECK(bs(127).word_size() == 2);
}

TEST_CASE("nexenne::container::bitset_dynamic highest bit of a full word is addressable") {
  bs b(128);
  nexenne::utility::ignore(b.set(63));
  nexenne::utility::ignore(b.set(64));
  nexenne::utility::ignore(b.set(127));
  CHECK(b[63]);
  CHECK(b[64]);
  CHECK(b[127]);
  CHECK(b.count() == 3);
  CHECK(b.find_first_set() == 63);
  CHECK(b.find_first_set(64) == 64);
  CHECK(b.find_first_set(65) == 127);
  CHECK(b.find_first_set(128) == 128);
}

TEST_CASE(
  "nexenne::container::bitset_dynamic find_first_set when only bit is in last partial word"
) {
  bs b(130);
  CHECK(b.find_first_set() == 130);
  nexenne::utility::ignore(b.set(129));
  CHECK(b.find_first_set() == 129);
  CHECK(b.find_first_set(129) == 129);
  CHECK(b.find_first_set(130) == 130);
  CHECK(b.find_first_set(200) == 130);

  std::vector<std::size_t> const indices(b.begin(), b.end());
  CHECK(indices == std::vector<std::size_t>{129});
  CHECK(b.count() == 1);
}

TEST_CASE("nexenne::container::bitset_dynamic find_first_set scans from inside a word") {
  bs b(64);
  nexenne::utility::ignore(b.set(10));
  nexenne::utility::ignore(b.set(40));
  CHECK(b.find_first_set(0) == 10);
  CHECK(b.find_first_set(10) == 10);
  CHECK(b.find_first_set(11) == 40);
  CHECK(b.find_first_set(40) == 40);
  CHECK(b.find_first_set(41) == 64);
}

TEST_CASE(
  "nexenne::container::bitset_dynamic set_bits iterates a full word and across the boundary"
) {
  bs b(128);
  nexenne::utility::ignore(b.set(0));
  nexenne::utility::ignore(b.set(63));
  nexenne::utility::ignore(b.set(64));
  nexenne::utility::ignore(b.set(127));
  std::vector<std::size_t> const indices(b.set_bits().begin(), b.set_bits().end());
  CHECK(indices == std::vector<std::size_t>{0, 63, 64, 127});

  auto it{b.begin()};
  auto const it_copy{it++};
  CHECK(*it_copy == 0);
  CHECK(*it == 63);
}

TEST_CASE(
  "nexenne::container::bitset_dynamic unary-complement equivalent via flip_all respects the tail"
) {
  bs b(65);
  b.flip_all();
  CHECK(b.count() == 65);
  CHECK(b.all());
  CHECK(b[64]);
  auto const w{b.words()};
  REQUIRE(w.size() == 2);
  CHECK(w[1] == std::uint64_t{1});
}

TEST_CASE(
  "nexenne::container::bitset_dynamic OR and XOR over mismatched widths use the common span"
) {
  bs wide(130);
  nexenne::utility::ignore(wide.set(100));
  nexenne::utility::ignore(wide.set(129));
  bs narrow(5);
  nexenne::utility::ignore(narrow.set(0));

  auto const o{wide | narrow};
  CHECK(o.size() == 130);
  CHECK(o[0]);
  CHECK(o[100]);
  CHECK(o[129]);
  CHECK(o.count() == 3);

  auto const x{wide ^ narrow};
  CHECK(x.size() == 130);
  CHECK(x[0]);
  CHECK(x[100]);
  CHECK(x[129]);

  auto const a{wide & narrow};
  CHECK(a.size() == 130);
  CHECK_FALSE(a[100]);
  CHECK_FALSE(a[129]);
  CHECK(a.count() == 0);
}

TEST_CASE("nexenne::container::bitset_dynamic compound bitwise assignment returns *this") {
  bs a{true, true, false, true};
  bs const b{true, false, true, false};
  auto& ref{a &= b};
  CHECK(&ref == &a);
  CHECK(a[0]);
  CHECK_FALSE(a[1]);
  CHECK_FALSE(a[2]);
  CHECK_FALSE(a[3]);

  bs c{true, false, false, false};
  (c ^= b) ^= b;
  CHECK(c[0]);
  CHECK_FALSE(c[1]);
  CHECK_FALSE(c[2]);
}

TEST_CASE("nexenne::container::bitset_dynamic flip-all then count equals size across boundaries") {
  for (auto const n :
       {std::size_t{1}, std::size_t{63}, std::size_t{64}, std::size_t{65}, std::size_t{200}}) {
    bs b(n);
    b.flip_all();
    CHECK(b.count() == n);
    CHECK(b.all());
  }
}

TEST_CASE("nexenne::container::bitset_dynamic copy assignment and self-assignment") {
  bs a(70);
  nexenne::utility::ignore(a.set(5));
  nexenne::utility::ignore(a.set(69));
  bs b(3);
  b = a;
  CHECK(b == a);
  CHECK(b.size() == 70);
  CHECK(b[5]);
  CHECK(b[69]);
  nexenne::utility::ignore(b.set(0));
  CHECK_FALSE(a[0]);

  auto* const self{&a};
  a = *self;
  CHECK(a.size() == 70);
  CHECK(a[5]);
  CHECK(a[69]);
  CHECK(a.count() == 2);
}

TEST_CASE("nexenne::container::bitset_dynamic move assignment zeros the source") {
  bs a(80);
  nexenne::utility::ignore(a.set(7));
  nexenne::utility::ignore(a.set(70));
  bs b(2);
  b = std::move(a);
  CHECK(b.size() == 80);
  CHECK(b[7]);
  CHECK(b[70]);
  CHECK(a.empty());
  CHECK(a.size() == 0);

  bs c(10);
  nexenne::utility::ignore(c.set(4));
  auto& self{c};
  c = std::move(self);
  CHECK(c.size() == 10);
  CHECK(c[4]);
}

TEST_CASE(
  "nexenne::container::bitset_dynamic resize growth initializes new bits and masks the tail"
) {
  bs zero(10);
  zero.set_all();
  zero.resize(200);
  CHECK(zero.size() == 200);
  CHECK(zero.count() == 10);
  for (std::size_t i{10}; i < 200; ++i) {
    CHECK_FALSE(zero[i]);
  }

  bs ones(60, true);
  ones.resize(70, true);
  CHECK(ones.size() == 70);
  CHECK(ones.count() == 70);
  ones.resize(128, true);
  CHECK(ones.count() == 128);
  CHECK(ones.all());

  bs grown;
  grown.resize(65, true);
  CHECK(grown.count() == 65);
}

TEST_CASE("nexenne::container::bitset_dynamic resize shrink drops high bits and remasks tail") {
  bs b(130);
  b.set_all();
  CHECK(b.count() == 130);
  b.resize(65);
  CHECK(b.size() == 65);
  CHECK(b.count() == 65);
  CHECK(b.all());
  CHECK(b.word_size() == 2);

  b.resize(64);
  CHECK(b.count() == 64);
  CHECK(b.word_size() == 1);

  b.resize(0);
  CHECK(b.empty());
  CHECK(b.count() == 0);
}

TEST_CASE(
  "nexenne::container::bitset_dynamic lexicographic ordering is on packed words then size"
) {
  bs lo{true, false};
  bs hi{false, true};
  CHECK((lo <=> hi) == std::strong_ordering::less);
  CHECK((hi <=> lo) == std::strong_ordering::greater);
  CHECK((lo <=> lo) == std::strong_ordering::equal);

  bs short_b{true};
  bs long_b{true, false};
  CHECK((short_b <=> long_b) == std::strong_ordering::less);

  CHECK(bs(64) != bs(65));
  CHECK(bs(64) == bs(64));
}

TEST_CASE("nexenne::container::bitset_dynamic const-correctness of read-only queries") {
  bs src(70);
  nexenne::utility::ignore(src.set(3));
  nexenne::utility::ignore(src.set(64));
  bs const b{src};
  CHECK(b.size() == 70);
  CHECK(b.word_size() == 2);
  CHECK(b[3]);
  CHECK(b[64]);
  CHECK(*b.test(3));
  CHECK_FALSE(*b.test(4));
  CHECK(b.count() == 2);
  CHECK(b.any());
  CHECK_FALSE(b.none());
  CHECK_FALSE(b.all());
  CHECK(b.find_first_set() == 3);
  CHECK_FALSE(b.empty());
  std::vector<std::size_t> const idx(b.begin(), b.end());
  CHECK(idx == std::vector<std::size_t>{3, 64});
  CHECK(b.words().size() == 2);
}

TEST_CASE("nexenne::container::bitset_dynamic capacity helpers preserve value") {
  CHECK(bs::max_size() == std::numeric_limits<std::size_t>::max() - (bs::bits_per_word - 1));

  bs b(5);
  nexenne::utility::ignore(b.set(2));
  b.reserve(1000);
  CHECK(b.size() == 5);
  CHECK(b[2]);
  CHECK(b.count() == 1);

  b.shrink_to_fit();
  CHECK(b.size() == 5);
  CHECK(b[2]);
  CHECK(b.count() == 1);

  b.clear();
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::bitset_dynamic member swap exchanges state") {
  bs a(10);
  nexenne::utility::ignore(a.set(1));
  bs b(70);
  nexenne::utility::ignore(b.set(64));
  a.swap(b);
  CHECK(a.size() == 70);
  CHECK(a[64]);
  CHECK(b.size() == 10);
  CHECK(b[1]);
}

static_assert(std::same_as<cn::bitset_dynamic::value_type, bool>);

TEST_CASE("nexenne::container::bitset_dynamic max_size is a non-wrapping bound") {
  constexpr auto max_index{std::numeric_limits<std::size_t>::max()};
  CHECK(cn::bitset_dynamic::max_size() == max_index - (cn::bitset_dynamic::bits_per_word - 1));
  // The largest safe width still computes a backing word count without wrapping:
  // ceil(max_size / 64) == SIZE_MAX / 64, not 0.
  auto const words_at_max{
    (cn::bitset_dynamic::max_size() + cn::bitset_dynamic::bits_per_word - 1)
    / cn::bitset_dynamic::bits_per_word
  };
  CHECK(words_at_max == max_index / cn::bitset_dynamic::bits_per_word);
  CHECK(words_at_max != 0);
}

static_assert([] {
  cn::bitset_dynamic const one_set{1};
  cn::bitset_dynamic const one_clear{0};
  cn::bitset_dynamic const sized(64);
  cn::bitset_dynamic const listed{true, false, true};
  return one_set.size() == 1 && one_set.count() == 1 && one_clear.size() == 1
         && one_clear.count() == 0 && sized.size() == 64 && sized.count() == 0 && listed.size() == 3
         && listed.count() == 2;
}());
static_assert(std::vector<bool>{1}.size() == 1 && std::vector<bool>(64).size() == 64);

}  // namespace
