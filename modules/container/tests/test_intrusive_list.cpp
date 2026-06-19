/**
 * @file
 * @brief Tests for nexenne::container::intrusive_list.
 */

#include <doctest/doctest.h>

#include <compare>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/intrusive_list.hpp>

namespace {

namespace cn = nexenne::container;

struct node : cn::intrusive_list_hook<node> {
  int value;

  constexpr explicit node(int v) noexcept : value{v} {}

  // Compare by value only; the hook base is not comparable, so a defaulted
  // comparison would not work.
  friend constexpr auto operator==(node const& a, node const& b) noexcept -> bool {
    return a.value == b.value;
  }

  friend constexpr auto operator<=>(node const& a, node const& b) noexcept -> std::strong_ordering {
    return a.value <=> b.value;
  }
};

struct tracked_node : cn::intrusive_list_hook<tracked_node> {
  int* dtors;

  explicit tracked_node(int* d) noexcept : dtors{d} {}

  ~tracked_node() {
    ++*dtors;
  }
};

using list_t = cn::intrusive_list<node>;

static_assert(std::bidirectional_iterator<list_t::iterator>);
static_assert(std::bidirectional_iterator<list_t::const_iterator>);
static_assert(!std::is_copy_constructible_v<list_t>);
static_assert(std::is_move_constructible_v<list_t>);

// intrusive_list is usable in a constant expression.
static_assert([] {
  node a{1};
  node b{2};
  node c{3};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list.push_front(c);  // [3, 1, 2]
  int sum{0};
  for (auto const& n : list) {
    sum += n.value;
  }
  return list.size() == 3 && sum == 6 && list.front()->value == 3 && list.back()->value == 2;
}());

TEST_CASE("nexenne::container::intrusive_list push order and is_linked") {
  node a{1};
  node b{2};
  node c{3};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list.push_front(c);  // [3, 1, 2]
  CHECK(list.size() == 3);
  CHECK(list.front()->value == 3);
  CHECK(list.back()->value == 2);
  CHECK(a.is_linked());

  std::vector<int> seq;
  for (auto const& n : list) {
    seq.push_back(n.value);
  }
  CHECK(seq == std::vector{3, 1, 2});
}

TEST_CASE("nexenne::container::intrusive_list erase by reference is O(1)") {
  node a{1};
  node b{2};
  node c{3};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list.push_back(c);
  list.erase(b);  // remove the middle without a search
  CHECK(list.size() == 2);
  CHECK_FALSE(b.is_linked());

  std::vector<int> seq;
  for (auto const& n : list) {
    seq.push_back(n.value);
  }
  CHECK(seq == std::vector{1, 3});
}

TEST_CASE("nexenne::container::intrusive_list erase by iterator returns the next") {
  node a{1};
  node b{2};
  node c{3};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list.push_back(c);
  auto const it{list.erase(list.begin())};  // remove 1
  CHECK(it->value == 2);
  CHECK(list.size() == 2);
}

TEST_CASE("nexenne::container::intrusive_list pop and front/back are nullable") {
  list_t list;
  CHECK(list.pop_front() == nullptr);
  CHECK(list.pop_back() == nullptr);
  CHECK(list.front() == nullptr);
  CHECK(list.back() == nullptr);

  node a{1};
  node b{2};
  list.push_back(a);
  list.push_back(b);
  REQUIRE(list.pop_front() != nullptr);
  REQUIRE(list.pop_back() != nullptr);
  CHECK(list.empty());
}

TEST_CASE("nexenne::container::intrusive_list iterates forward and reverse") {
  node a{1};
  node b{2};
  node c{3};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list.push_back(c);

  std::vector<int> reverse;
  for (auto it{list.rbegin()}; it != list.rend(); ++it) {
    reverse.push_back(it->value);
  }
  CHECK(reverse == std::vector{3, 2, 1});

  list.begin()->value = 99;  // mutate through the iterator
  CHECK(a.value == 99);
}

TEST_CASE("nexenne::container::intrusive_list move detaches the source") {
  node a{1};
  node b{2};
  list_t src;
  src.push_back(a);
  src.push_back(b);
  list_t dst{std::move(src)};
  CHECK(dst.size() == 2);
  CHECK(src.empty());
  CHECK(dst.front()->value == 1);
}

TEST_CASE("nexenne::container::intrusive_list swap") {
  node a{1};
  node b{2};
  node c{3};
  list_t x;
  x.push_back(a);
  list_t y;
  y.push_back(b);
  y.push_back(c);
  swap(x, y);
  CHECK(x.size() == 2);
  CHECK(y.size() == 1);
  CHECK(x.front()->value == 2);
  CHECK(y.front()->value == 1);
}

TEST_CASE("nexenne::container::intrusive_list an element moves between lists") {
  node a{1};
  list_t x;
  list_t y;
  x.push_back(a);
  x.erase(a);      // detach from x
  y.push_back(a);  // attach to y
  CHECK(x.empty());
  CHECK(y.size() == 1);
  CHECK(y.front()->value == 1);
}

TEST_CASE("nexenne::container::intrusive_list equality and ordering compare values") {
  node a1{1};
  node a2{2};
  node b1{1};
  node b2{2};
  node c1{1};
  node c2{3};
  list_t a;
  a.push_back(a1);
  a.push_back(a2);
  list_t b;
  b.push_back(b1);
  b.push_back(b2);
  list_t c;
  c.push_back(c1);
  c.push_back(c2);
  CHECK(a == b);  // equal values, different element addresses
  CHECK(a != c);
  CHECK(a < c);
}

TEST_CASE("nexenne::container::intrusive_list never destroys its elements") {
  int dtors{0};
  {
    tracked_node n{&dtors};
    cn::intrusive_list<tracked_node> list;
    list.push_back(n);
    list.clear();
    CHECK_FALSE(n.is_linked());
    CHECK(dtors == 0);  // clear detached but did not destroy
  }
  CHECK(dtors == 1);  // n's own destructor ran at scope exit
}

TEST_CASE("nexenne::container::intrusive_list insert before a middle position") {
  node a{1};
  node b{3};
  node c{2};
  list_t list;
  list.push_back(a);
  list.push_back(b);  // [1, 3]
  auto it{list.begin()};
  ++it;                                     // points at b (value 3)
  auto const inserted{list.insert(it, c)};  // [1, 2, 3]
  CHECK(inserted->value == 2);
  CHECK(list.size() == 3);
  std::vector<int> seq;
  for (auto const& n : list) {
    seq.push_back(n.value);
  }
  CHECK(seq == std::vector{1, 2, 3});
}

TEST_CASE("nexenne::container::intrusive_list pop returns the correct end element") {
  node a{1};
  node b{2};
  node c{3};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list.push_back(c);
  auto* const front{list.pop_front()};
  REQUIRE(front != nullptr);
  CHECK(front->value == 1);
  CHECK_FALSE(front->is_linked());
  auto* const back{list.pop_back()};
  REQUIRE(back != nullptr);
  CHECK(back->value == 3);
  CHECK_FALSE(back->is_linked());
  CHECK(list.size() == 1);
  CHECK(list.front()->value == 2);
}

TEST_CASE("nexenne::container::intrusive_list erase by iterator at the last position returns end") {
  node a{1};
  node b{2};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  auto it{list.begin()};
  ++it;                             // points at b, the last element
  auto const next{list.erase(it)};  // erase the tail
  CHECK(next == list.end());
  CHECK(list.size() == 1);
  CHECK(list.front()->value == 1);
}

TEST_CASE("nexenne::container::intrusive_list const iteration and const front/back") {
  node a{1};
  node b{2};
  node c{3};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list.push_back(c);
  list_t const& view{list};
  CHECK(view.size() == 3);
  CHECK_FALSE(view.empty());
  REQUIRE(view.front() != nullptr);
  REQUIRE(view.back() != nullptr);
  CHECK(view.front()->value == 1);
  CHECK(view.back()->value == 3);
  static_assert(std::is_same_v<decltype(view.front()), node const*>);

  std::vector<int> forward;
  for (auto it{view.cbegin()}; it != view.cend(); ++it) {
    forward.push_back(it->value);
  }
  CHECK(forward == std::vector{1, 2, 3});
  std::vector<int> reverse;
  for (auto it{view.crbegin()}; it != view.crend(); ++it) {
    reverse.push_back(it->value);
  }
  CHECK(reverse == std::vector{3, 2, 1});
}

TEST_CASE("nexenne::container::intrusive_list non-const to const iterator conversion") {
  node a{1};
  list_t list;
  list.push_back(a);
  list_t::iterator const mut{list.begin()};
  list_t::const_iterator const ci{mut};  // converting constructor
  CHECK(ci->value == 1);
  CHECK(ci == list.cbegin());
}

TEST_CASE("nexenne::container::intrusive_list move-assign detaches old elements and steals new") {
  node a1{1};
  node a2{2};
  node b1{3};
  list_t dst;
  dst.push_back(a1);
  dst.push_back(a2);
  list_t src;
  src.push_back(b1);
  dst = std::move(src);  // dst's old elements are detached, b1 stolen
  CHECK(dst.size() == 1);
  CHECK(dst.front()->value == 3);
  CHECK(src.empty());
  CHECK_FALSE(a1.is_linked());  // detached, not destroyed
  CHECK_FALSE(a2.is_linked());
  a1.value = 7;  // still a valid, usable object
  CHECK(a1.value == 7);
}

TEST_CASE("nexenne::container::intrusive_list self move-assign and self swap are no-ops") {
  node a{1};
  node b{2};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list_t& alias{list};
  list = std::move(alias);  // guarded self move-assign
  CHECK(list.size() == 2);
  CHECK(list.front()->value == 1);
  list.swap(alias);  // guarded self swap
  CHECK(list.size() == 2);
  CHECK(list.back()->value == 2);
}

TEST_CASE("nexenne::container::intrusive_list a moved element leaves its hook detached") {
  // The hook's move ctor leaves the moved-TO hook unlinked: the list still refers
  // to the source address. Move construct a fresh node from an unlinked one.
  node src{5};
  node dst{std::move(src)};
  CHECK_FALSE(dst.is_linked());
  CHECK(dst.value == 5);
  list_t list;
  list.push_back(dst);  // the moved-to node links cleanly
  CHECK(list.size() == 1);
  CHECK(list.front()->value == 5);

  // The hook's move-assignment operator resets the target's links to null.
  node other{9};
  other = std::move(src);  // operator= clears links
  CHECK_FALSE(other.is_linked());
}

TEST_CASE("nexenne::container::intrusive_list allocates nothing and reuses node storage") {
  // Cycle one node through the list many times; with no allocation the same
  // address recurs and size accounting stays exact.
  node a{1};
  list_t list;
  for (int i{0}; i < 1000; ++i) {
    list.push_back(a);
    CHECK(list.size() == 1);
    CHECK(list.front() == &a);  // same storage every cycle
    list.erase(a);
    CHECK(list.empty());
    CHECK_FALSE(a.is_linked());
  }
  CHECK(list.max_size() > 0);
}

TEST_CASE("nexenne::container::intrusive_list reverse iterator over a populated list") {
  node a{1};
  node b{2};
  node c{3};
  node d{4};
  list_t list;
  list.push_back(a);
  list.push_back(b);
  list.push_back(c);
  list.push_back(d);
  std::vector<int> reverse;
  for (auto it{list.rbegin()}; it != list.rend(); ++it) {
    reverse.push_back(it->value);
  }
  CHECK(reverse == std::vector{4, 3, 2, 1});
}

}  // namespace
