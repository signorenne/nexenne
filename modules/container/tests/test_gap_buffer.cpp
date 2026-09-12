/**
 * @file
 * @brief Tests for nexenne::container::gap_buffer.
 */

#include <doctest/doctest.h>

#include <any>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/gap_buffer.hpp>

namespace {

namespace cn = nexenne::container;
using gb = cn::gap_buffer<int>;

static_assert(std::random_access_iterator<gb::iterator>);
static_assert(std::random_access_iterator<gb::const_iterator>);

static_assert([] {
  gb b;
  b.insert(10);
  b.insert(30);
  if (!b.move_cursor_to(1).has_value()) {
    return false;
  }
  b.insert(20);
  return b.size() == 3 && b[0] == 10 && b[1] == 20 && b[2] == 30;
}());

TEST_CASE("nexenne::container::gap_buffer default and initializer list") {
  gb b;
  CHECK(b.empty());
  CHECK(b.cursor() == 0);

  gb c{1, 2, 3};
  CHECK(c.size() == 3);
  CHECK(c.cursor() == 3);
  CHECK(c[0] == 1);
  CHECK(c[2] == 3);
}

TEST_CASE("nexenne::container::gap_buffer insert at a moved cursor") {
  gb b{1, 2, 4, 5};
  REQUIRE(b.move_cursor_to(2).has_value());
  CHECK(b.cursor() == 2);
  b.insert(3);
  CHECK(b.size() == 5);
  for (int i{0}; i < 5; ++i) {
    CHECK(b[static_cast<std::size_t>(i)] == i + 1);
  }
}

TEST_CASE("nexenne::container::gap_buffer erase forward and backward") {
  gb b{1, 2, 3, 4};
  REQUIRE(b.move_cursor_to(2).has_value());
  REQUIRE(b.erase_forward().has_value());
  CHECK(b.size() == 3);
  CHECK(b[2] == 4);
  REQUIRE(b.erase_backward().has_value());
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
  CHECK(b[1] == 4);
  CHECK(b.cursor() == 1);
}

TEST_CASE("nexenne::container::gap_buffer erase at the edges errors") {
  gb b{1};
  REQUIRE(b.move_cursor_to(1).has_value());
  CHECK(b.erase_forward().error() == cn::container_error::empty);
  REQUIRE(b.move_cursor_to(0).has_value());
  CHECK(b.erase_backward().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::gap_buffer cursor moves and bounds") {
  gb b{1, 2, 3};
  CHECK(b.move_cursor_to(10).error() == cn::container_error::out_of_range);
  CHECK(b.move_cursor_by(-100).error() == cn::container_error::out_of_range);
  REQUIRE(b.move_cursor_by(-2).has_value());
  CHECK(b.cursor() == 1);
}

TEST_CASE("nexenne::container::gap_buffer at, front, back") {
  gb b{10, 20, 30};
  REQUIRE(b.at(1).has_value());
  CHECK(**b.at(1) == 20);
  CHECK(b.at(3).error() == cn::container_error::out_of_range);
  REQUIRE(b.front() != nullptr);
  CHECK(*b.front() == 10);
  REQUIRE(b.back() != nullptr);
  CHECK(*b.back() == 30);

  gb e;
  CHECK(e.front() == nullptr);
  CHECK(e.back() == nullptr);
}

TEST_CASE("nexenne::container::gap_buffer iteration regardless of gap position") {
  gb b{1, 2, 3, 4};
  REQUIRE(b.move_cursor_to(2).has_value());

  std::vector<int> const forward(b.begin(), b.end());
  CHECK(forward == std::vector{1, 2, 3, 4});
  std::vector<int> const reverse(b.rbegin(), b.rend());
  CHECK(reverse == std::vector{4, 3, 2, 1});

  *b.begin() = 99;
  CHECK(b[0] == 99);
  CHECK(*(b.begin() + 3) == 4);
  CHECK(b.end() - b.begin() == 4);
}

TEST_CASE("nexenne::container::gap_buffer equality and ordering ignore the gap") {
  gb a{1, 2, 3};
  gb b{1, 2, 3};
  gb c{1, 2, 4};
  CHECK(a == b);
  CHECK(a != c);
  CHECK(a < c);
  REQUIRE(b.move_cursor_to(1).has_value());
  CHECK(a == b);
}

TEST_CASE("nexenne::container::gap_buffer growth preserves order") {
  gb b;
  for (int i{0}; i < 100; ++i) {
    b.insert(i);
  }
  CHECK(b.size() == 100);
  for (int i{0}; i < 100; ++i) {
    CHECK(b[static_cast<std::size_t>(i)] == i);
  }
}

TEST_CASE("nexenne::container::gap_buffer reserve, shrink, clear") {
  gb b{1, 2, 3};
  b.reserve(200);
  CHECK(b.capacity() >= 200);
  CHECK(b.size() == 3);
  b.shrink_to_fit();
  CHECK(b.size() == 3);
  CHECK(b[0] == 1);
  CHECK(b.cursor() == 3);
  b.clear();
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::gap_buffer holds a move-only type") {
  cn::gap_buffer<std::unique_ptr<int>> b;
  b.insert(std::make_unique<int>(1));
  b.emplace(std::make_unique<int>(2));
  CHECK(b.size() == 2);
  CHECK(*b[0] == 1);
  CHECK(*b[1] == 2);
  REQUIRE(b.move_cursor_to(1).has_value());
  REQUIRE(b.erase_backward().has_value());
  CHECK(b.size() == 1);
  CHECK(*b[0] == 2);
}

TEST_CASE("nexenne::container::gap_buffer moved-from source is a valid empty buffer") {
  cn::gap_buffer<int> a;
  for (int i = 0; i < 20; ++i) {
    a.insert(i);
  }
  cn::gap_buffer<int> b{std::move(a)};
  CHECK(b.size() == 20);
  CHECK(a.empty());
  CHECK(a.size() == 0);
  a.insert(99);
  CHECK(a.size() == 1);
  CHECK(a[0] == 99);

  cn::gap_buffer<int> c;
  c = std::move(b);
  CHECK(c.size() == 20);
  CHECK(b.empty());
  CHECK(b.size() == 0);
}

TEST_CASE("nexenne::container::gap_buffer cursor move on a closed gap keeps non-trivial elements") {
  cn::gap_buffer<std::string> g;
  for (std::size_t i = 0; i < cn::gap_buffer<std::string>::initial_gap; ++i) {
    g.insert(std::string(6, static_cast<char>('a' + (i % 26))));
  }
  REQUIRE(g.size() == cn::gap_buffer<std::string>::initial_gap);
  REQUIRE(g.move_cursor_to(0).has_value());
  for (std::size_t i = 0; i < g.size(); ++i) {
    CHECK(g[i].size() == 6);
  }
  REQUIRE(g.move_cursor_to(g.size() / 2).has_value());
  for (std::size_t i = 0; i < g.size(); ++i) {
    CHECK(g[i].size() == 6);
  }
  g.shrink_to_fit();
  CHECK(g.size() == cn::gap_buffer<std::string>::initial_gap);
  for (std::size_t i = 0; i < g.size(); ++i) {
    CHECK(g[i].size() == 6);
  }
}

TEST_CASE("nexenne::container::gap_buffer copy preserves the logical sequence with a live gap") {
  cn::gap_buffer<std::string> a{"one", "two", "three", "four"};
  REQUIRE(a.move_cursor_to(2).has_value());
  cn::gap_buffer<std::string> b{a};
  CHECK(b.size() == 4);
  CHECK(b[0] == "one");
  CHECK(b[3] == "four");
  CHECK(a == b);
  a[0] = "edited";
  CHECK(b[0] == "one");

  cn::gap_buffer<std::string> c;
  c = a;
  CHECK(c.size() == 4);
  CHECK(c[0] == "edited");
  c[1] = "x";
  CHECK(a[1] == "two");
}

TEST_CASE("nexenne::container::gap_buffer self copy- and move-assignment are safe") {
  cn::gap_buffer<std::string> g{"a", "bb", "ccc"};
  REQUIRE(g.move_cursor_to(1).has_value());
  cn::gap_buffer<std::string>& alias{g};
  g = alias;
  CHECK(g.size() == 3);
  CHECK(g[0] == "a");
  CHECK(g[2] == "ccc");

  g = std::move(alias);
  CHECK(g.size() == 3);
  CHECK(g[1] == "bb");
}

TEST_CASE("nexenne::container::gap_buffer const access and const iteration") {
  gb const b{10, 20, 30};
  CHECK(b.size() == 3);
  CHECK_FALSE(b.empty());
  CHECK(b[0] == 10);
  CHECK(b[2] == 30);
  REQUIRE(b.at(1).has_value());
  CHECK(**b.at(1) == 20);
  CHECK(b.at(9).error() == cn::container_error::out_of_range);
  REQUIRE(b.front() != nullptr);
  CHECK(*b.front() == 10);
  REQUIRE(b.back() != nullptr);
  CHECK(*b.back() == 30);
  static_assert(std::is_same_v<decltype(b[0]), int const&>);
  static_assert(std::is_same_v<decltype(b.front()), int const*>);

  std::vector<int> const forward(b.begin(), b.end());
  CHECK(forward == std::vector{10, 20, 30});
  std::vector<int> const reverse(b.rbegin(), b.rend());
  CHECK(reverse == std::vector{30, 20, 10});
  std::vector<int> const cforward(b.cbegin(), b.cend());
  CHECK(cforward == std::vector{10, 20, 30});
  std::vector<int> const creverse(b.crbegin(), b.crend());
  CHECK(creverse == std::vector{30, 20, 10});
}

TEST_CASE("nexenne::container::gap_buffer non-const to const iterator conversion") {
  gb b{1, 2, 3};
  gb::iterator const mut{b.begin()};
  gb::const_iterator ci{mut};
  CHECK(*ci == 1);
  CHECK(b.end() - b.begin() == 3);
  CHECK(b.cend() - b.cbegin() == 3);
}

TEST_CASE("nexenne::container::gap_buffer move_cursor_by forward and to the edges") {
  gb b{1, 2, 3, 4, 5};
  CHECK(b.cursor() == 5);
  REQUIRE(b.move_cursor_to(0).has_value());
  REQUIRE(b.move_cursor_by(3).has_value());
  CHECK(b.cursor() == 3);
  b.insert(99);
  CHECK(b[3] == 99);
  CHECK(b[5] == 5);
  REQUIRE(b.move_cursor_by(0).has_value());
  CHECK(b.cursor() == 4);
}

TEST_CASE("nexenne::container::gap_buffer emplace forwards constructor arguments") {
  cn::gap_buffer<std::string> b;
  b.emplace(std::size_t{5}, 'q');
  b.emplace(std::size_t{3}, 'r');
  REQUIRE(b.move_cursor_to(1).has_value());
  b.emplace(std::size_t{2}, 's');
  CHECK(b.size() == 3);
  CHECK(b[0] == "qqqqq");
  CHECK(b[1] == "ss");
  CHECK(b[2] == "rrr");
}

TEST_CASE("nexenne::container::gap_buffer erase the entire buffer one element at a time") {
  gb b{1, 2, 3};
  REQUIRE(b.move_cursor_to(0).has_value());
  REQUIRE(b.erase_forward().has_value());
  REQUIRE(b.erase_forward().has_value());
  REQUIRE(b.erase_forward().has_value());
  CHECK(b.empty());
  CHECK(b.erase_forward().error() == cn::container_error::empty);
  CHECK(b.erase_backward().error() == cn::container_error::empty);
  b.insert(7);
  CHECK(b.size() == 1);
  CHECK(b[0] == 7);
}

TEST_CASE("nexenne::container::gap_buffer cursor move with a real (open) gap shifts strings") {
  cn::gap_buffer<std::string> g{"alpha", "beta", "gamma", "delta", "epsilon"};
  CHECK(g.cursor() == 5);
  for (std::size_t target{0}; target <= g.size(); ++target) {
    REQUIRE(g.move_cursor_to(target).has_value());
    CHECK(g.cursor() == target);
  }
  CHECK(g[0] == "alpha");
  CHECK(g[1] == "beta");
  CHECK(g[2] == "gamma");
  CHECK(g[3] == "delta");
  CHECK(g[4] == "epsilon");
  REQUIRE(g.move_cursor_to(0).has_value());
  g.insert("zero");
  CHECK(g[0] == "zero");
  CHECK(g[1] == "alpha");
  CHECK(g.size() == 6);
}

TEST_CASE("nexenne::container::gap_buffer shrink_to_fit with an open gap closes it correctly") {
  cn::gap_buffer<std::string> g{"x", "yy", "zzz", "wwww"};
  REQUIRE(g.move_cursor_to(2).has_value());
  g.shrink_to_fit();
  CHECK(g.size() == 4);
  CHECK(g.cursor() == 4);
  CHECK(g[0] == "x");
  CHECK(g[1] == "yy");
  CHECK(g[2] == "zzz");
  CHECK(g[3] == "wwww");
}

TEST_CASE("nexenne::container::gap_buffer max_size is positive and swap exchanges state") {
  gb a{1, 2};
  gb b{7, 8, 9};
  CHECK(a.max_size() > 0);
  swap(a, b);
  CHECK(a.size() == 3);
  CHECK(b.size() == 2);
  CHECK(a[0] == 7);
  CHECK(b[1] == 2);
}

TEST_CASE("nexenne::container::gap_buffer insert accepts an argument aliasing its storage") {
  gb b;
  for (auto i{0}; i < 16; ++i) {
    b.insert(i);
  }
  REQUIRE(b.size() == 16);
  b.insert(b[0]);
  REQUIRE(b.size() == 17);
  CHECK(b[16] == 0);

  cn::gap_buffer<std::string> s;
  for (auto i{0}; i < 16; ++i) {
    s.insert(std::string(32, static_cast<char>('a' + i)));
  }
  REQUIRE(s.size() == 16);
  auto const first{*s.front()};
  s.insert(std::move(*s.front()));
  REQUIRE(s.size() == 17);
  CHECK(**s.at(16) == first);
}

struct slide_counter {
  static inline long long move_assigns{0};
  int v{0};

  slide_counter() noexcept = default;

  slide_counter(int const x) noexcept : v{x} {}

  slide_counter(slide_counter const&) noexcept = default;
  slide_counter(slide_counter&&) noexcept = default;
  auto operator=(slide_counter const&) noexcept -> slide_counter& = default;

  auto operator=(slide_counter&& other) noexcept -> slide_counter& {
    v = other.v;
    ++move_assigns;
    return *this;
  }

  ~slide_counter() noexcept = default;
};

TEST_CASE("nexenne::container::gap_buffer mid-buffer insert stays amortised O(1)") {
  auto const per_insert{[](std::size_t const post_count) -> double {
    cn::gap_buffer<slide_counter> g;
    for (std::size_t i{0}; i < post_count; ++i) {
      g.insert(slide_counter{static_cast<int>(i)});
    }
    REQUIRE(g.move_cursor_to(0).has_value());
    slide_counter::move_assigns = 0;
    slide_counter const value{7};
    for (std::size_t i{0}; i < post_count; ++i) {
      g.insert(value);
    }
    return static_cast<double>(slide_counter::move_assigns) / static_cast<double>(post_count);
  }};

  auto const small{per_insert(1000)};
  auto const large{per_insert(4000)};

  CHECK(small < 20.0);
  CHECK(large < 20.0);
  CHECK(large < small * 2.0 + 5.0);
}

TEST_CASE("nexenne::container::gap_buffer insert into a full gap copies the element exactly") {
  using any_vec = std::vector<std::any>;
  any_vec const three(3, std::any{1});
  cn::gap_buffer<any_vec> g;
  g.insert(three);
  any_vec moved(3, std::any{2});
  g.insert(std::move(moved));
  REQUIRE(g.size() == 2);
  CHECK(g[0].size() == 3);
  CHECK(g[1].size() == 3);
}

static_assert(!std::is_copy_constructible_v<cn::gap_buffer<std::unique_ptr<int>>>);
static_assert(!std::is_copy_assignable_v<cn::gap_buffer<std::unique_ptr<int>>>);
static_assert(std::is_move_constructible_v<cn::gap_buffer<std::unique_ptr<int>>>);
static_assert(std::is_copy_constructible_v<cn::gap_buffer<int>>);
static_assert(std::is_copy_assignable_v<cn::gap_buffer<int>>);

TEST_CASE("nexenne::container::gap_buffer at returns result like deque") {
  gb v{10, 20, 30};
  auto const hit{v.at(1)};
  REQUIRE(hit.has_value());
  CHECK(**hit == 20);
  **v.at(1) = 21;
  CHECK(v[1] == 21);
  auto const miss{v.at(v.size())};
  REQUIRE_FALSE(miss.has_value());
  CHECK(miss.error() == cn::container_error::out_of_range);
  gb const& cv{v};
  static_assert(std::is_same_v<decltype(v.at(0)), cn::result<int*>>);
  static_assert(std::is_same_v<decltype(cv.at(0)), cn::result<int const*>>);
  CHECK(**cv.at(0) == 10);
  CHECK(cv.at(99).error() == cn::container_error::out_of_range);
}

template <typename B>
concept moves_cursor = requires(B b) { b.move_cursor_to(std::size_t{0}); };
static_assert(moves_cursor<cn::gap_buffer<char>&>);
static_assert(!moves_cursor<cn::gap_buffer<char> const&>);
static_assert(
  std::is_same_v<decltype(std::declval<cn::gap_buffer<char> const&>().cursor()), std::size_t>
);

TEST_CASE("nexenne::container::gap_buffer reads the cursor and moves it only when mutable") {
  cn::gap_buffer<char> b{'a', 'b', 'c'};
  CHECK(b.cursor() == 3);
  REQUIRE(b.move_cursor_to(1).has_value());
  CHECK(b.cursor() == 1);
  CHECK(b.move_cursor_to(4).error() == cn::container_error::out_of_range);
  CHECK(b.cursor() == 1);
  b.insert('x');
  CHECK(std::string(b.begin(), b.end()) == "axbc");
}

}  // namespace
