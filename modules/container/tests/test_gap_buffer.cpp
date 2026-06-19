/**
 * @file
 * @brief Tests for nexenne::container::gap_buffer.
 */

#include <doctest/doctest.h>

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

// gap_buffer is usable in a constant expression.
static_assert([] {
  gb b;
  b.insert(10);
  b.insert(30);  // [10, 30], cursor at 2
  if (!b.move_cursor_to(1).has_value()) {
    return false;
  }
  b.insert(20);  // [10, 20, 30]
  return b.size() == 3 && b[0] == 10 && b[1] == 20 && b[2] == 30;
}());

TEST_CASE("nexenne::container::gap_buffer default and initializer list") {
  gb b;
  CHECK(b.empty());
  CHECK(b.cursor() == 0);

  gb c{1, 2, 3};
  CHECK(c.size() == 3);
  CHECK(c.cursor() == 3);  // cursor lands at the end
  CHECK(c[0] == 1);
  CHECK(c[2] == 3);
}

TEST_CASE("nexenne::container::gap_buffer insert at a moved cursor") {
  gb b{1, 2, 4, 5};
  REQUIRE(b.move_cursor_to(2).has_value());  // between 2 and 4
  CHECK(b.cursor() == 2);
  b.insert(3);  // [1, 2, 3, 4, 5]
  CHECK(b.size() == 5);
  for (int i{0}; i < 5; ++i) {
    CHECK(b[static_cast<std::size_t>(i)] == i + 1);
  }
}

TEST_CASE("nexenne::container::gap_buffer erase forward and backward") {
  gb b{1, 2, 3, 4};
  REQUIRE(b.move_cursor_to(2).has_value());  // between 2 and 3
  REQUIRE(b.erase_forward().has_value());    // remove 3 -> [1, 2, 4]
  CHECK(b.size() == 3);
  CHECK(b[2] == 4);
  REQUIRE(b.erase_backward().has_value());  // remove 2 -> [1, 4]
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
  CHECK(b[1] == 4);
  CHECK(b.cursor() == 1);
}

TEST_CASE("nexenne::container::gap_buffer erase at the edges errors") {
  gb b{1};
  REQUIRE(b.move_cursor_to(1).has_value());  // cursor at the end
  CHECK(b.erase_forward().error() == cn::container_error::empty);
  REQUIRE(b.move_cursor_to(0).has_value());  // cursor at the start
  CHECK(b.erase_backward().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::gap_buffer cursor moves and bounds") {
  gb b{1, 2, 3};
  CHECK(b.move_cursor_to(10).error() == cn::container_error::out_of_range);
  CHECK(b.move_cursor_by(-100).error() == cn::container_error::out_of_range);
  REQUIRE(b.move_cursor_by(-2).has_value());  // 3 -> 1
  CHECK(b.cursor() == 1);
}

TEST_CASE("nexenne::container::gap_buffer at, front, back") {
  gb b{10, 20, 30};
  REQUIRE(b.at(1) != nullptr);
  CHECK(*b.at(1) == 20);
  CHECK(b.at(3) == nullptr);  // out of range
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
  REQUIRE(b.move_cursor_to(2).has_value());  // gap bisects the sequence

  std::vector<int> const forward(b.begin(), b.end());
  CHECK(forward == std::vector{1, 2, 3, 4});
  std::vector<int> const reverse(b.rbegin(), b.rend());
  CHECK(reverse == std::vector{4, 3, 2, 1});

  *b.begin() = 99;  // mutate through the iterator
  CHECK(b[0] == 99);
  CHECK(*(b.begin() + 3) == 4);     // random access
  CHECK(b.end() - b.begin() == 4);  // difference
}

TEST_CASE("nexenne::container::gap_buffer equality and ordering ignore the gap") {
  gb a{1, 2, 3};
  gb b{1, 2, 3};
  gb c{1, 2, 4};
  CHECK(a == b);
  CHECK(a != c);
  CHECK(a < c);
  REQUIRE(b.move_cursor_to(1).has_value());  // gap position must not matter
  CHECK(a == b);
}

TEST_CASE("nexenne::container::gap_buffer growth preserves order") {
  gb b;
  for (int i{0}; i < 100; ++i) {
    b.insert(i);  // forces grow_gap several times
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
  CHECK(b.cursor() == 3);  // shrink closes the gap at the end
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
  REQUIRE(b.erase_backward().has_value());  // remove the 1
  CHECK(b.size() == 1);
  CHECK(*b[0] == 2);
}

TEST_CASE("nexenne::container::gap_buffer moved-from source is a valid empty buffer") {
  cn::gap_buffer<int> a;
  for (int i = 0; i < 20; ++i) {
    a.insert(i);  // build a real gap layout
  }
  cn::gap_buffer<int> b{std::move(a)};
  CHECK(b.size() == 20);
  // The defaulted move would leave a's gap indices stale over an emptied buffer,
  // underflowing size(); the user-defined move resets them to a usable empty state.
  CHECK(a.empty());
  CHECK(a.size() == 0);
  a.insert(99);  // still usable after being moved from
  CHECK(a.size() == 1);
  CHECK(a[0] == 99);

  cn::gap_buffer<int> c;
  c = std::move(b);
  CHECK(c.size() == 20);
  CHECK(b.empty());
  CHECK(b.size() == 0);
}

TEST_CASE("nexenne::container::gap_buffer cursor move on a closed gap keeps non-trivial elements") {
  // Filling exactly initial_gap elements closes the gap (gap_size() == 0). Moving
  // the cursor then must not self-move-assign each element, which would empty a
  // std::string while leaving an int untouched (so int-only tests miss this).
  cn::gap_buffer<std::string> g;
  for (std::size_t i = 0; i < cn::gap_buffer<std::string>::initial_gap; ++i) {
    g.insert(std::string(6, static_cast<char>('a' + (i % 26))));
  }
  REQUIRE(g.size() == cn::gap_buffer<std::string>::initial_gap);
  REQUIRE(g.move_cursor_to(0).has_value());  // gap is empty here
  for (std::size_t i = 0; i < g.size(); ++i) {
    CHECK(g[i].size() == 6);  // not emptied by a self-move
  }
  REQUIRE(g.move_cursor_to(g.size() / 2).has_value());
  for (std::size_t i = 0; i < g.size(); ++i) {
    CHECK(g[i].size() == 6);
  }
  g.shrink_to_fit();  // closes an already-closed gap: must not self-move either
  CHECK(g.size() == cn::gap_buffer<std::string>::initial_gap);
  for (std::size_t i = 0; i < g.size(); ++i) {
    CHECK(g[i].size() == 6);
  }
}

TEST_CASE("nexenne::container::gap_buffer copy preserves the logical sequence with a live gap") {
  cn::gap_buffer<std::string> a{"one", "two", "three", "four"};
  REQUIRE(a.move_cursor_to(2).has_value());  // gap bisects the sequence
  cn::gap_buffer<std::string> b{a};          // copy with an open gap
  CHECK(b.size() == 4);
  CHECK(b[0] == "one");
  CHECK(b[3] == "four");
  CHECK(a == b);  // independent of gap position
  a[0] = "edited";
  CHECK(b[0] == "one");  // deep, independent copy

  cn::gap_buffer<std::string> c;
  c = a;  // copy-assign
  CHECK(c.size() == 4);
  CHECK(c[0] == "edited");
  c[1] = "x";
  CHECK(a[1] == "two");  // still independent
}

TEST_CASE("nexenne::container::gap_buffer self copy- and move-assignment are safe") {
  cn::gap_buffer<std::string> g{"a", "bb", "ccc"};
  REQUIRE(g.move_cursor_to(1).has_value());
  cn::gap_buffer<std::string>& alias{g};
  g = alias;  // defaulted self copy-assign
  CHECK(g.size() == 3);
  CHECK(g[0] == "a");
  CHECK(g[2] == "ccc");

  g = std::move(alias);  // self move-assign returns early, leaving g intact
  CHECK(g.size() == 3);
  CHECK(g[1] == "bb");
}

TEST_CASE("nexenne::container::gap_buffer const access and const iteration") {
  gb const b{10, 20, 30};
  CHECK(b.size() == 3);
  CHECK_FALSE(b.empty());
  CHECK(b[0] == 10);
  CHECK(b[2] == 30);
  REQUIRE(b.at(1) != nullptr);
  CHECK(*b.at(1) == 20);
  CHECK(b.at(9) == nullptr);
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
  gb::const_iterator ci{mut};  // converting constructor
  CHECK(*ci == 1);
  CHECK(b.end() - b.begin() == 3);
  CHECK(b.cend() - b.cbegin() == 3);
}

TEST_CASE("nexenne::container::gap_buffer move_cursor_by forward and to the edges") {
  gb b{1, 2, 3, 4, 5};
  CHECK(b.cursor() == 5);
  REQUIRE(b.move_cursor_to(0).has_value());
  REQUIRE(b.move_cursor_by(3).has_value());  // forward shift through the gap
  CHECK(b.cursor() == 3);
  b.insert(99);  // [1, 2, 3, 99, 4, 5]
  CHECK(b[3] == 99);
  CHECK(b[5] == 5);
  REQUIRE(b.move_cursor_by(0).has_value());  // no-op
  CHECK(b.cursor() == 4);
}

TEST_CASE("nexenne::container::gap_buffer emplace forwards constructor arguments") {
  cn::gap_buffer<std::string> b;
  // std::size_t counts: string(size_type, char) avoids an int->size_type
  // sign-conversion warning when the args are forwarded.
  b.emplace(std::size_t{5}, 'q');  // string(count, char)
  b.emplace(std::size_t{3}, 'r');
  REQUIRE(b.move_cursor_to(1).has_value());
  b.emplace(std::size_t{2}, 's');  // inserted between
  CHECK(b.size() == 3);
  CHECK(b[0] == "qqqqq");
  CHECK(b[1] == "ss");
  CHECK(b[2] == "rrr");
}

TEST_CASE("nexenne::container::gap_buffer erase the entire buffer one element at a time") {
  gb b{1, 2, 3};
  REQUIRE(b.move_cursor_to(0).has_value());
  REQUIRE(b.erase_forward().has_value());  // [2, 3]
  REQUIRE(b.erase_forward().has_value());  // [3]
  REQUIRE(b.erase_forward().has_value());  // []
  CHECK(b.empty());
  CHECK(b.erase_forward().error() == cn::container_error::empty);
  CHECK(b.erase_backward().error() == cn::container_error::empty);
  b.insert(7);  // still usable
  CHECK(b.size() == 1);
  CHECK(b[0] == 7);
}

TEST_CASE("nexenne::container::gap_buffer cursor move with a real (open) gap shifts strings") {
  // Build fewer than initial_gap elements so the gap stays open, then sweep the
  // cursor end-to-end: every move relocates a std::string across the gap, which
  // must leave each element intact (no leaks, no emptied strings).
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
  // Insert at the front after a full sweep: order must hold.
  REQUIRE(g.move_cursor_to(0).has_value());
  g.insert("zero");
  CHECK(g[0] == "zero");
  CHECK(g[1] == "alpha");
  CHECK(g.size() == 6);
}

TEST_CASE("nexenne::container::gap_buffer shrink_to_fit with an open gap closes it correctly") {
  cn::gap_buffer<std::string> g{"x", "yy", "zzz", "wwww"};
  REQUIRE(g.move_cursor_to(2).has_value());  // open gap in the middle
  g.shrink_to_fit();
  CHECK(g.size() == 4);
  CHECK(g.cursor() == 4);  // gap closed at the end
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
  // Fill exactly to a closed gap so the next insert must grow_gap (reallocating
  // the backing vector). Passing an element of the same buffer would dangle if
  // the value were read after the reallocation.
  gb b;
  for (auto i{0}; i < 16; ++i) {  // initial_gap == 16
    b.insert(i);
  }
  REQUIRE(b.size() == 16);
  b.insert(b[0]);  // lvalue aliasing this buffer; b[0] == 0
  REQUIRE(b.size() == 17);
  CHECK(b[16] == 0);

  // Same hazard on the move overload, with a heap-allocating element type so a
  // use-after-realloc trips the sanitizer on the string's own storage too.
  cn::gap_buffer<std::string> s;
  for (auto i{0}; i < 16; ++i) {
    s.insert(std::string(32, static_cast<char>('a' + i)));
  }
  REQUIRE(s.size() == 16);
  auto const first{*s.front()};  // copy of s[0] for comparison
  s.insert(std::move(*s.front()));
  REQUIRE(s.size() == 17);
  CHECK(*s.at(16) == first);
}

}  // namespace
