/**
 * @file
 * @brief Tests for nexenne::container::small_vector.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <any>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/small_vector.hpp>

namespace {

namespace cn = nexenne::container;
using sv = cn::small_vector<int, 4>;

static_assert(sv::inline_capacity() == 4);

static_assert(cn::small_vector<int, 8>::inline_capacity() == 8);
static_assert(cn::small_vector<int, 0>::inline_capacity() == 0);
static_assert(
  cn::small_vector<int, 4>::max_size() == std::numeric_limits<std::size_t>::max() / sizeof(int)
);
static_assert(
  cn::small_vector<std::int64_t, 4>::max_size()
  == std::numeric_limits<std::size_t>::max() / sizeof(std::int64_t)
);

TEST_CASE("nexenne::container::small_vector push_back of an existing element survives regrow") {
  cn::small_vector<std::string, 1> v;
  std::string const seed{"a string long enough to force a heap allocation, well past SSO"};
  v.push_back(seed);
  for (int i{0}; i < 8; ++i) {
    v.push_back(v[0]);
  }
  CHECK(v.size() == 9);
  for (auto const& s : v) {
    CHECK(s == seed);
  }
}

TEST_CASE("nexenne::container::small_vector stays inline up to N, then heap") {
  sv v;
  CHECK(v.is_inline());
  CHECK(v.capacity() == 4);
  for (int i{0}; i < 4; ++i) {
    v.push_back(i);
  }
  CHECK(v.is_inline());
  CHECK(v.size() == 4);

  v.push_back(4);
  CHECK_FALSE(v.is_inline());
  CHECK(v.capacity() >= 5);
  CHECK(v.size() == 5);
  CHECK(v[4] == 4);
}

TEST_CASE("nexenne::container::small_vector pop_back and empty error") {
  sv v{1, 2};
  CHECK(v.pop_back().has_value());
  CHECK(v.pop_back().has_value());
  CHECK(v.pop_back().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::small_vector emplace_back returns the new element") {
  sv v;
  auto& slot{v.emplace_back(42)};
  CHECK(slot == 42);
  slot = 7;
  CHECK(v[0] == 7);
}

TEST_CASE("nexenne::container::small_vector at/front/back bounds-checked") {
  sv v{10, 20};
  CHECK(**v.at(1) == 20);
  CHECK(v.at(2).error() == cn::container_error::out_of_range);
  CHECK(*v.front() == 10);
  CHECK(*v.back() == 20);

  sv empty;
  CHECK(empty.front() == nullptr);
  CHECK(empty.back() == nullptr);
}

TEST_CASE("nexenne::container::small_vector iteration and span") {
  sv v{1, 2, 3};
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));
  CHECK(v.span().size() == 3);
  int sum{0};
  for (int const x : v) {
    sum += x;
  }
  CHECK(sum == 6);
}

TEST_CASE("nexenne::container::small_vector copy is independent (heap)") {
  sv a{1, 2, 3, 4, 5};
  CHECK_FALSE(a.is_inline());
  sv b{a};
  CHECK(b == a);
  a[0] = 99;
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::small_vector move steals the heap allocation") {
  sv a{1, 2, 3, 4, 5};
  auto const* const data_before{a.data()};
  sv b{std::move(a)};
  CHECK(b.size() == 5);
  CHECK(b.data() == data_before);
  CHECK(a.empty());
  CHECK(a.is_inline());
}

TEST_CASE("nexenne::container::small_vector move of inline contents") {
  sv a{1, 2};
  sv b{std::move(a)};
  CHECK(b.size() == 2);
  CHECK(b[1] == 2);
  CHECK(a.empty());
}

TEST_CASE("nexenne::container::small_vector copy and move assignment") {
  sv const a{1, 2, 3, 4, 5};
  sv b;
  b = a;
  CHECK(b == a);

  sv source{1, 2, 3, 4, 5};
  sv c;
  c = std::move(source);
  CHECK(c.size() == 5);
  CHECK(source.empty());
}

TEST_CASE("nexenne::container::small_vector swap mixes inline and heap") {
  sv a{1, 2};
  sv b{1, 2, 3, 4, 5, 6};
  swap(a, b);
  CHECK(a.size() == 6);
  CHECK_FALSE(a.is_inline());
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::small_vector reserve then shrink_to_fit back to inline") {
  sv v{1, 2, 3};
  v.reserve(100);
  CHECK(v.capacity() >= 100);
  CHECK_FALSE(v.is_inline());

  v.shrink_to_fit();
  CHECK(v.is_inline());
  CHECK(v.capacity() == 4);
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));
}

TEST_CASE("nexenne::container::small_vector shrink_to_fit to exact heap size") {
  sv v;
  for (int i{0}; i < 10; ++i) {
    v.push_back(i);
  }
  v.reserve(64);
  CHECK(v.capacity() >= 64);
  v.shrink_to_fit();
  CHECK(v.capacity() == 10);
  CHECK_FALSE(v.is_inline());
}

TEST_CASE("nexenne::container::small_vector assign overloads") {
  sv v{9, 9};
  v.assign({1, 2, 3});
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));
  v.assign(std::size_t{4}, 7);
  CHECK(v.size() == 4);
  CHECK(v[3] == 7);
  std::array<int, 2> const src{5, 6};
  v.assign(src.begin(), src.end());
  CHECK(std::ranges::equal(v, std::array{5, 6}));
}

TEST_CASE("nexenne::container::small_vector comparison") {
  CHECK(sv{1, 2, 3} == sv{1, 2, 3});
  CHECK(sv{1, 2} != sv{1, 2, 3});
  CHECK(sv{1, 2, 3} < sv{1, 2, 4});
}

TEST_CASE("nexenne::container::small_vector holds a move-only type across growth") {
  cn::small_vector<std::unique_ptr<int>, 2> v;
  for (int i{0}; i < 5; ++i) {
    v.push_back(std::make_unique<int>(i));
  }
  CHECK(v.size() == 5);
  CHECK(*v[4] == 4);
}

TEST_CASE("nexenne::container::small_vector destroys elements and frees heap") {
  auto tracker{std::make_shared<int>(0)};
  {
    cn::small_vector<std::shared_ptr<int>, 2> v;
    v.push_back(tracker);
    v.push_back(tracker);
    v.push_back(tracker);
    CHECK(tracker.use_count() == 4);
  }
  CHECK(tracker.use_count() == 1);
}

namespace {
struct move_counter {
  int* copies;
  int* moves;

  move_counter(int* c, int* m) noexcept : copies{c}, moves{m} {}

  move_counter(move_counter const& other) noexcept : copies{other.copies}, moves{other.moves} {
    ++*copies;
  }

  move_counter(move_counter&& other) noexcept : copies{other.copies}, moves{other.moves} {
    ++*moves;
  }
};
}  // namespace

TEST_CASE("nexenne::container::small_vector push_back picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::small_vector<move_counter, 4> v;
  move_counter c{&copies, &moves};

  v.push_back(c);
  CHECK(copies == 1);
  CHECK(moves == 0);

  v.push_back(std::move(c));
  CHECK(copies == 1);
  CHECK(moves == 1);
}

TEST_CASE("nexenne::container::small_vector with zero inline capacity uses heap at once") {
  cn::small_vector<int, 0> v;
  CHECK(v.capacity() == 0);
  v.push_back(1);
  CHECK_FALSE(v.is_inline());
  CHECK(v[0] == 1);
  CHECK(v.size() == 1);
}

TEST_CASE("nexenne::container::small_vector default-constructed state") {
  sv v;
  CHECK(v.empty());
  CHECK(v.size() == 0);
  CHECK(v.capacity() == 4);
  CHECK(v.is_inline());
  CHECK(v.begin() == v.end());
  CHECK(v.cbegin() == v.cend());
  CHECK(v.span().empty());
  CHECK(v.data() != nullptr);
}

TEST_CASE("nexenne::container::small_vector empty transitions across push/pop/clear") {
  sv v;
  CHECK(v.empty());
  v.push_back(1);
  CHECK_FALSE(v.empty());
  CHECK(v.pop_back().has_value());
  CHECK(v.empty());
  v.push_back(7);
  v.clear();
  CHECK(v.empty());
  CHECK(v.size() == 0);
  CHECK(v.capacity() == 4);
}

TEST_CASE("nexenne::container::small_vector single element boundary") {
  sv v;
  v.push_back(42);
  CHECK(v.size() == 1);
  CHECK(v.is_inline());
  CHECK(*v.front() == 42);
  CHECK(*v.back() == 42);
  CHECK(v.front() == v.back());
  CHECK(v.pop_back().has_value());
  CHECK(v.empty());
}

TEST_CASE("nexenne::container::small_vector exactly at inline capacity stays inline") {
  sv v{1, 2, 3, 4};
  CHECK(v.size() == 4);
  CHECK(v.capacity() == 4);
  CHECK(v.is_inline());
}

TEST_CASE("nexenne::container::small_vector one past inline capacity spills to heap") {
  sv v{1, 2, 3, 4};
  CHECK(v.is_inline());
  v.push_back(5);
  CHECK_FALSE(v.is_inline());
  CHECK(v.capacity() >= 5);
  CHECK(std::ranges::equal(v, std::array{1, 2, 3, 4, 5}));
}

TEST_CASE("nexenne::container::small_vector reserve exactly N keeps inline") {
  sv v{1, 2};
  v.reserve(4);
  CHECK(v.is_inline());
  CHECK(v.capacity() == 4);
  v.reserve(2);
  CHECK(v.capacity() == 4);
}

TEST_CASE("nexenne::container::small_vector growth doubles capacity") {
  sv v;
  for (int i{0}; i < 4; ++i) {
    v.push_back(i);
  }
  v.push_back(4);
  CHECK(v.capacity() == 8);
  for (int i{5}; i < 8; ++i) {
    v.push_back(i);
  }
  CHECK(v.capacity() == 8);
  v.push_back(8);
  CHECK(v.capacity() == 16);
}

TEST_CASE("nexenne::container::small_vector push_back of own element while inline (no grow)") {
  sv v{10, 20, 30};
  v.push_back(v[0]);
  CHECK(v.size() == 4);
  CHECK(std::ranges::equal(v, std::array{10, 20, 30, 10}));
}

TEST_CASE("nexenne::container::small_vector push_back of own element triggers inline->heap grow") {
  sv v{1, 2, 3, 4};
  v.push_back(v[3]);
  CHECK(v.size() == 5);
  CHECK_FALSE(v.is_inline());
  CHECK(std::ranges::equal(v, std::array{1, 2, 3, 4, 4}));
}

TEST_CASE("nexenne::container::small_vector emplace_back of own element survives grow") {
  cn::small_vector<std::string, 2> v;
  std::string const seed{"a deliberately long string well past the SSO threshold for libstdc++"};
  v.push_back(seed);
  v.push_back(seed);
  v.emplace_back(*v.front());
  CHECK(v.size() == 3);
  CHECK_FALSE(v.is_inline());
  for (auto const& s : v) {
    CHECK(s == seed);
  }
}

TEST_CASE("nexenne::container::small_vector push_back own element across heap->heap regrow") {
  cn::small_vector<int, 2> v{1, 2, 3};
  while (v.capacity() != v.size()) {
    v.push_back(0);
  }
  auto const fill_count{v.size()};
  v.push_back(v[0]);
  CHECK(v.size() == fill_count + 1);
  CHECK(v.back() != nullptr);
  CHECK(*v.back() == 1);
}

TEST_CASE("nexenne::container::small_vector self copy-assignment is a no-op") {
  sv v{1, 2, 3, 4, 5};
  sv& alias{v};
  v = alias;
  CHECK(v.size() == 5);
  CHECK(std::ranges::equal(v, std::array{1, 2, 3, 4, 5}));
}

TEST_CASE("nexenne::container::small_vector self move-assignment is a no-op") {
  sv v{1, 2, 3, 4, 5};
  auto* const self{&v};
  v = std::move(*self);
  CHECK(v.size() == 5);
  CHECK(std::ranges::equal(v, std::array{1, 2, 3, 4, 5}));
}

TEST_CASE("nexenne::container::small_vector self swap is a no-op") {
  sv v{1, 2, 3, 4, 5};
  v.swap(v);
  CHECK(std::ranges::equal(v, std::array{1, 2, 3, 4, 5}));
}

TEST_CASE("nexenne::container::small_vector addresses stable while inline") {
  sv v;
  v.push_back(1);
  int const* const p0{&v[0]};
  v.push_back(2);
  v.push_back(3);
  CHECK(&v[0] == p0);
  CHECK(v.is_inline());
}

TEST_CASE("nexenne::container::small_vector grow past inline invalidates addresses") {
  sv v{1, 2, 3, 4};
  int const* const inline_addr{&v[0]};
  CHECK(v.is_inline());
  v.push_back(5);
  CHECK_FALSE(v.is_inline());
  CHECK(&v[0] != inline_addr);
}

TEST_CASE("nexenne::container::small_vector heap addresses stable until next grow") {
  sv v{1, 2, 3, 4};
  v.reserve(8);
  CHECK(v.capacity() == 8);
  int const* const p0{&v[0]};
  v.push_back(5);
  v.push_back(6);
  CHECK(&v[0] == p0);
}

TEST_CASE("nexenne::container::small_vector moved-from is empty and reusable (heap source)") {
  sv a{1, 2, 3, 4, 5};
  sv b{std::move(a)};
  CHECK(a.empty());
  CHECK(a.is_inline());
  CHECK(a.capacity() == 4);
  a.push_back(99);
  a.push_back(100);
  CHECK(a.size() == 2);
  CHECK(std::ranges::equal(a, std::array{99, 100}));
}

TEST_CASE("nexenne::container::small_vector moved-from is reusable (move-assign source)") {
  sv a{1, 2};
  sv b;
  b = std::move(a);
  CHECK(a.empty());
  a.assign({7, 8, 9, 10, 11});
  CHECK(a.size() == 5);
  CHECK(std::ranges::equal(a, std::array{7, 8, 9, 10, 11}));
}

TEST_CASE("nexenne::container::small_vector copy-construct inline source stays inline") {
  sv a{1, 2, 3};
  sv b{a};
  CHECK(b.is_inline());
  CHECK(b == a);
  b[0] = 99;
  CHECK(a[0] == 1);
}

TEST_CASE("nexenne::container::small_vector copy-assign heap-over-inline and inline-over-heap") {
  sv big{1, 2, 3, 4, 5, 6};
  sv small{0};
  small = big;
  CHECK(small.size() == 6);
  CHECK_FALSE(small.is_inline());
  CHECK(small == big);

  sv heap_target{9, 9, 9, 9, 9, 9, 9};
  sv tiny{1, 2};
  heap_target = tiny;
  CHECK(heap_target.size() == 2);
  CHECK(std::ranges::equal(heap_target, std::array{1, 2}));
}

TEST_CASE("nexenne::container::small_vector move-assign inline-over-heap and heap-over-inline") {
  sv heap_target{1, 2, 3, 4, 5, 6};
  sv inline_src{7, 8};
  heap_target = std::move(inline_src);
  CHECK(heap_target.size() == 2);
  CHECK(heap_target.is_inline());
  CHECK(std::ranges::equal(heap_target, std::array{7, 8}));
  CHECK(inline_src.empty());

  sv inline_target{1};
  sv heap_src{10, 20, 30, 40, 50};
  auto const* const stolen{heap_src.data()};
  inline_target = std::move(heap_src);
  CHECK(inline_target.size() == 5);
  CHECK(inline_target.data() == stolen);
  CHECK(heap_src.empty());
}

TEST_CASE("nexenne::container::small_vector self-assign-like via swap of two heaps") {
  sv a{1, 2, 3, 4, 5};
  sv b{6, 7, 8, 9, 10, 11};
  auto const* const ad{a.data()};
  auto const* const bd{b.data()};
  a.swap(b);
  CHECK(a.data() == bd);
  CHECK(b.data() == ad);
  CHECK(a.size() == 6);
  CHECK(b.size() == 5);
}

TEST_CASE("nexenne::container::small_vector of std::string copy/move/grow") {
  cn::small_vector<std::string, 2> v;
  v.push_back("alpha");
  v.push_back(std::string{"beta"});
  std::string movable{"gamma is long enough to live on the heap, beyond SSO storage"};
  v.push_back(std::move(movable));
  CHECK(v.size() == 3);
  CHECK(v[0] == "alpha");
  CHECK(v[2] == "gamma is long enough to live on the heap, beyond SSO storage");

  cn::small_vector<std::string, 2> copy{v};
  CHECK(copy == v);
  copy[0] = "changed";
  CHECK(v[0] == "alpha");
}

TEST_CASE("nexenne::container::small_vector move-only unique_ptr move-construct and swap") {
  cn::small_vector<std::unique_ptr<int>, 2> a;
  for (int i{0}; i < 4; ++i) {
    a.push_back(std::make_unique<int>(i));
  }
  cn::small_vector<std::unique_ptr<int>, 2> b{std::move(a)};
  CHECK(b.size() == 4);
  CHECK(*b[3] == 3);
  CHECK(a.empty());

  cn::small_vector<std::unique_ptr<int>, 2> c;
  c.push_back(std::make_unique<int>(99));
  swap(b, c);
  CHECK(c.size() == 4);
  CHECK(b.size() == 1);
  CHECK(*b[0] == 99);
}

TEST_CASE("nexenne::container::small_vector move-only pop_back destroys element") {
  cn::small_vector<std::unique_ptr<int>, 2> v;
  v.push_back(std::make_unique<int>(1));
  v.push_back(std::make_unique<int>(2));
  CHECK(v.pop_back().has_value());
  CHECK(v.size() == 1);
  CHECK(*v[0] == 1);
}

TEST_CASE("nexenne::container::small_vector const overloads and accessors") {
  sv const v{10, 20, 30};
  CHECK(v.size() == 3);
  CHECK(v[1] == 20);
  CHECK(**v.at(2) == 30);
  CHECK(v.at(3).error() == cn::container_error::out_of_range);
  CHECK(*v.front() == 10);
  CHECK(*v.back() == 30);
  CHECK(v.data()[0] == 10);
  CHECK(v.span().size() == 3);

  int sum{0};
  for (int const x : v) {
    sum += x;
  }
  CHECK(sum == 60);

  CHECK(std::accumulate(v.cbegin(), v.cend(), 0) == 60);
  CHECK(v.cbegin() == v.begin());
}

TEST_CASE("nexenne::container::small_vector reverse iterators forward and const") {
  sv v{1, 2, 3};
  std::vector<int> reversed;
  for (auto it{v.rbegin()}; it != v.rend(); ++it) {
    reversed.push_back(*it);
  }
  CHECK(reversed == std::vector<int>{3, 2, 1});

  sv const cv{4, 5, 6};
  std::vector<int> creversed;
  for (auto it{cv.crbegin()}; it != cv.crend(); ++it) {
    creversed.push_back(*it);
  }
  CHECK(creversed == std::vector<int>{6, 5, 4});
  CHECK(cv.rbegin() == cv.crbegin());
}

TEST_CASE("nexenne::container::small_vector comparison across inline/heap boundary") {
  sv inline_v{1, 2, 3};
  sv heap_v{1, 2, 3};
  heap_v.reserve(64);
  CHECK_FALSE(heap_v.is_inline());
  CHECK(inline_v == heap_v);
  CHECK_FALSE(inline_v != heap_v);

  CHECK(sv{1, 2} < sv{1, 2, 3});
  CHECK(sv{1, 2, 3} > sv{1, 2});
  CHECK(sv{1, 2, 3} <= sv{1, 2, 3});
  CHECK(sv{1, 2, 3} >= sv{1, 2, 3});
  CHECK(sv{1, 2, 4} > sv{1, 2, 3});

  CHECK(sv{} == sv{});
  CHECK(sv{} < sv{0});

  auto const ordering{sv{1, 2, 3} <=> sv{1, 2, 4}};
  CHECK(ordering == std::strong_ordering::less);
}

TEST_CASE("nexenne::container::small_vector differential vs std::vector model") {
  std::mt19937 rng{0xC0FFEEu};
  std::uniform_int_distribution<int> op_dist{0, 6};
  std::uniform_int_distribution<int> val_dist{-1000, 1000};

  cn::small_vector<int, 4> sut;
  std::vector<int> model;

  auto check_equal{[&] {
    REQUIRE(sut.size() == model.size());
    CHECK(std::ranges::equal(sut, model));
  }};

  for (int step{0}; step < 5000; ++step) {
    switch (op_dist(rng)) {
      case 0:
      case 1: {
        int const val{val_dist(rng)};
        sut.push_back(val);
        model.push_back(val);
        break;
      }
      case 2: {
        if (!model.empty()) {
          std::uniform_int_distribution<std::size_t> idx_dist{0, model.size() - 1};
          std::size_t const i{idx_dist(rng)};
          sut.push_back(sut[i]);
          model.push_back(model[i]);
        }
        break;
      }
      case 3: {
        if (model.empty()) {
          CHECK(sut.pop_back().error() == cn::container_error::empty);
        } else {
          CHECK(sut.pop_back().has_value());
          model.pop_back();
        }
        break;
      }
      case 4: {
        int const val{val_dist(rng)};
        CHECK(sut.emplace_back(val) == val);
        model.push_back(val);
        break;
      }
      case 5: {
        if ((step % 37) == 0) {
          sut.clear();
          model.clear();
        }
        break;
      }
      case 6: {
        if ((step % 2) == 0) {
          std::uniform_int_distribution<std::size_t> cap_dist{0, 32};
          sut.reserve(cap_dist(rng));
        } else {
          sut.shrink_to_fit();
        }
        break;
      }
      default:
        break;
    }
    check_equal();
  }
  check_equal();
}

TEST_CASE("nexenne::container::small_vector emplace_back grow path matches construct_at") {
  cn::small_vector<std::vector<int>, 1> grow;
  grow.emplace_back(1, 9);
  REQUIRE(grow.size() == 1);
  auto& grown{grow.emplace_back(3, 5)};
  CHECK(grown.size() == 3);
  CHECK(grown[0] == 5);
  CHECK(grow.back()->size() == 3);

  cn::small_vector<std::vector<int>, 4> hot;
  auto& warm{hot.emplace_back(3, 5)};
  CHECK(warm.size() == 3);
  CHECK(warm == grown);
}

TEST_CASE(
  "nexenne::container::small_vector swap keeps sizes for an element constructible from the vector"
) {
  cn::small_vector<std::any, 4> a;
  a.push_back(1);
  a.push_back(2);
  cn::small_vector<std::any, 4> b;
  b.push_back(3);
  a.swap(b);
  REQUIRE(a.size() == 1);
  REQUIRE(b.size() == 2);
  CHECK(std::any_cast<int>(a[0]) == 3);
  CHECK(std::any_cast<int>(b[0]) == 1);
  CHECK(std::any_cast<int>(b[1]) == 2);
}

static_assert(!std::is_copy_constructible_v<cn::small_vector<std::unique_ptr<int>, 4>>);
static_assert(!std::is_copy_assignable_v<cn::small_vector<std::unique_ptr<int>, 4>>);
static_assert(std::is_move_constructible_v<cn::small_vector<std::unique_ptr<int>, 4>>);
static_assert(std::is_copy_constructible_v<cn::small_vector<int, 4>>);
static_assert(std::is_copy_assignable_v<cn::small_vector<int, 4>>);

TEST_CASE("nexenne::container::small_vector at returns result like deque") {
  sv v{10, 20, 30};
  auto const hit{v.at(1)};
  REQUIRE(hit.has_value());
  CHECK(**hit == 20);
  **v.at(1) = 21;
  CHECK(v[1] == 21);
  auto const miss{v.at(v.size())};
  REQUIRE_FALSE(miss.has_value());
  CHECK(miss.error() == cn::container_error::out_of_range);
  sv const& cv{v};
  static_assert(std::is_same_v<decltype(v.at(0)), cn::result<int*>>);
  static_assert(std::is_same_v<decltype(cv.at(0)), cn::result<int const*>>);
  CHECK(**cv.at(0) == 10);
  CHECK(cv.at(99).error() == cn::container_error::out_of_range);
}

}  // namespace
