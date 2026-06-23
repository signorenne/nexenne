/**
 * @file
 * @brief Tests for nexenne::container::ring_buffer.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;
using rb = cn::ring_buffer<int, 3>;

static_assert(rb::capacity() == 3);
static_assert(std::forward_iterator<rb::iterator>);
static_assert(std::forward_iterator<rb::const_iterator>);

TEST_CASE("nexenne::container::ring_buffer push_overwrite of the evicted element when full") {
  cn::ring_buffer<std::string, 2> r;
  std::string const oldest{"oldest string, long enough to live on the heap rather than SSO"};
  nexenne::utility::discard(r.push(oldest));
  nexenne::utility::discard(
    r.push("newer string, also heap allocated to avoid the small-string buffer")
  );
  std::string const expected{*r.front()};  // the element about to be evicted
  r.push_overwrite(*r.front());            // aliases the evicted slot
  CHECK(*r.back() == expected);
}

// Drive a ring_buffer entirely at compile time: push to full, pop, wrap.
static_assert([] {
  rb r;
  r.push(1);
  r.push(2);
  r.push(3);
  bool ok{r.full() && r[0] == 1 && r[2] == 3};
  auto const first{r.pop()};
  ok = ok && first.has_value() && *first == 1 && r.size() == 2;
  r.push(4);  // tail wraps into the freed slot
  return ok && r[0] == 2 && r[1] == 3 && r[2] == 4;
}());

TEST_CASE("nexenne::container::ring_buffer push, full, pop FIFO, empty") {
  rb r;
  CHECK(r.empty());
  CHECK(r.push(1).has_value());
  CHECK(r.push(2).has_value());
  CHECK(r.push(3).has_value());
  CHECK(r.full());
  CHECK(r.push(4).error() == cn::container_error::full);
  CHECK(*r.front() == 1);
  CHECK(*r.back() == 3);
  CHECK(*r.pop() == 1);  // FIFO
  CHECK(*r.pop() == 2);
  CHECK(*r.pop() == 3);
  CHECK(r.pop().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::ring_buffer wraps around preserving order") {
  rb r;
  r.push(1);
  r.push(2);
  r.push(3);
  CHECK(*r.pop() == 1);  // head advances
  r.push(4);             // tail wraps into the freed slot
  CHECK(r.size() == 3);
  CHECK(r[0] == 2);
  CHECK(r[1] == 3);
  CHECK(r[2] == 4);

  std::vector<int> seen;
  for (int const x : r) {
    seen.push_back(x);
  }
  CHECK(seen == std::vector{2, 3, 4});  // iteration is FIFO
}

TEST_CASE("nexenne::container::ring_buffer power-of-two capacity wraps (mask path)") {
  cn::ring_buffer<int, 4> r;  // power of two uses the mask wrap
  for (int i{0}; i < 4; ++i) {
    r.push(i);  // [0, 1, 2, 3]
  }
  CHECK(r.pop().has_value());  // drop 0
  CHECK(r.pop().has_value());  // drop 1
  r.push(4);
  r.push(5);  // tail wraps via the mask
  CHECK(r.size() == 4);
  CHECK(r[0] == 2);
  CHECK(r[1] == 3);
  CHECK(r[2] == 4);
  CHECK(r[3] == 5);
}

TEST_CASE("nexenne::container::ring_buffer push_overwrite keeps the most recent N") {
  rb r;
  for (int i{0}; i < 5; ++i) {
    r.push_overwrite(i);  // 0,1,2,3,4 -> keeps 2,3,4
  }
  CHECK(r.size() == 3);
  CHECK(r[0] == 2);
  CHECK(r[1] == 3);
  CHECK(r[2] == 4);
}

TEST_CASE("nexenne::container::ring_buffer emplace constructs in place") {
  cn::ring_buffer<std::pair<int, int>, 2> r;
  CHECK(r.emplace(1, 2).has_value());
  CHECK(r.front()->first == 1);
  CHECK(r.emplace(3, 4).has_value());
  CHECK(r.emplace(5, 6).error() == cn::container_error::full);
}

TEST_CASE("nexenne::container::ring_buffer at is bounds-checked") {
  rb r;
  r.push(10);
  r.push(20);
  CHECK(*r.at(1) == 20);
  CHECK(r.at(2) == nullptr);

  rb empty;
  CHECK(empty.front() == nullptr);
  CHECK(empty.back() == nullptr);
}

TEST_CASE("nexenne::container::ring_buffer copy and move preserve FIFO order across a wrap") {
  rb a;
  a.push(1);
  a.push(2);
  CHECK(a.pop().has_value());  // head now at index 1
  a.push(3);
  a.push(4);  // logical order [2, 3, 4] with head != 0

  rb const b{a};  // copy canonicalises head to 0
  CHECK(b.size() == 3);
  CHECK(b[0] == 2);
  CHECK(b[1] == 3);
  CHECK(b[2] == 4);

  rb c{std::move(a)};
  CHECK(c.size() == 3);
  CHECK(c[0] == 2);
  CHECK(c[2] == 4);
  CHECK(a.empty());
}

TEST_CASE("nexenne::container::ring_buffer swap") {
  rb a;
  a.push(1);
  a.push(2);
  rb b;
  b.push(7);
  swap(a, b);
  CHECK(a.size() == 1);
  CHECK(a[0] == 7);
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::ring_buffer holds a move-only type") {
  cn::ring_buffer<std::unique_ptr<int>, 2> r;
  CHECK(r.push(std::make_unique<int>(5)).has_value());
  CHECK(r.emplace(std::make_unique<int>(6)).has_value());
  CHECK(r.push(std::make_unique<int>(7)).error() == cn::container_error::full);
  auto const popped{r.pop()};
  CHECK(popped.has_value());
  CHECK(**popped == 5);
}

TEST_CASE("nexenne::container::ring_buffer destroys its elements") {
  auto tracker{std::make_shared<int>(0)};
  {
    cn::ring_buffer<std::shared_ptr<int>, 3> r;
    r.push(tracker);
    r.push(tracker);
    CHECK(tracker.use_count() == 3);  // tracker + two stored
    r.push_overwrite(tracker);
    r.push_overwrite(tracker);  // evicts oldest, net still tracker + three
    CHECK(tracker.use_count() == 4);
  }
  CHECK(tracker.use_count() == 1);  // destructor destroyed all stored
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

TEST_CASE("nexenne::container::ring_buffer push picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::ring_buffer<move_counter, 4> r;
  move_counter c{&copies, &moves};

  CHECK(r.push(c).has_value());  // lvalue: one copy, no move
  CHECK(copies == 1);
  CHECK(moves == 0);

  CHECK(r.push(std::move(c)).has_value());  // rvalue: one move
  CHECK(copies == 1);
  CHECK(moves == 1);
}

TEST_CASE("nexenne::container::ring_buffer back after wrap, assignment, move-only overwrite") {
  rb r;
  CHECK(r.push(1).has_value());
  CHECK(r.push(2).has_value());
  CHECK(r.push(3).has_value());
  CHECK(r.pop().has_value());    // head advances
  CHECK(r.push(4).has_value());  // tail wrapped to 0; newest is 4
  CHECK(*r.back() == 4);         // back() via wrap(m_tail + N - 1)

  rb a;
  CHECK(a.push(10).has_value());
  CHECK(a.push(20).has_value());
  rb b;
  CHECK(b.push(99).has_value());
  b = a;  // copy-assign replaces b's contents
  CHECK(b.size() == 2);
  CHECK(b[0] == 10);
  rb c;
  c = std::move(a);  // move-assign
  CHECK(c.size() == 2);
  CHECK(c[1] == 20);

  cn::ring_buffer<std::unique_ptr<int>, 2> m;
  m.push_overwrite(std::make_unique<int>(1));
  m.push_overwrite(std::make_unique<int>(2));
  m.push_overwrite(std::make_unique<int>(3));  // evicts the oldest by move
  CHECK(m.size() == 2);
  CHECK(*m[1] == 3);
}

// A type that counts live instances and flags any double-destruction, used to
// assert eviction destroys the oldest slot exactly once.
namespace {
struct life_counter {
  static inline int alive{0};
  static inline int constructed{0};
  static inline int destroyed{0};
  static inline bool double_free{false};
  int value{0};
  bool live{false};

  life_counter() noexcept : value{0} {
    enter();
  }

  explicit life_counter(int v) noexcept : value{v} {
    enter();
  }

  life_counter(life_counter const& other) noexcept : value{other.value} {
    enter();
  }

  life_counter(life_counter&& other) noexcept : value{other.value} {
    enter();
  }

  auto operator=(life_counter const& other) noexcept -> life_counter& {
    value = other.value;
    return *this;
  }

  auto operator=(life_counter&& other) noexcept -> life_counter& {
    value = other.value;
    return *this;
  }

  ~life_counter() noexcept {
    if (!live) {
      double_free = true;
    }
    live = false;
    --alive;
    ++destroyed;
  }

  auto enter() noexcept -> void {
    live = true;
    ++alive;
    ++constructed;
  }

  static auto reset() noexcept -> void {
    alive = 0;
    constructed = 0;
    destroyed = 0;
    double_free = false;
  }
};
}  // namespace

TEST_CASE("nexenne::container::ring_buffer N == 1 boundary: full after one push, FIFO of one") {
  cn::ring_buffer<int, 1> r;
  CHECK(r.empty());
  CHECK(r.capacity() == 1);
  CHECK(r.max_size() == 1);
  CHECK(r.push(7).has_value());
  CHECK(r.full());
  CHECK(*r.front() == 7);
  CHECK(*r.back() == 7);  // sole element is both front and back
  CHECK(r.push(8).error() == cn::container_error::full);
  CHECK(*r.pop() == 7);
  CHECK(r.empty());
  CHECK(r.pop().error() == cn::container_error::empty);

  // push_overwrite on N == 1 simply replaces the single element each time.
  r.push_overwrite(1);
  r.push_overwrite(2);
  r.push_overwrite(3);
  CHECK(r.size() == 1);
  CHECK(*r.front() == 3);
}

TEST_CASE("nexenne::container::ring_buffer multi-wrap keeps FIFO order across many laps") {
  cn::ring_buffer<int, 3> r;  // non-power-of-two: exercises compare-subtract wrap
  int next{0};
  r.push(next++);
  r.push(next++);
  r.push(next++);
  // Steady state: pop the oldest, push the next, many times so head and tail
  // each lap the slot array several times over.
  for (int lap{0}; lap < 20; ++lap) {
    auto const popped{r.pop()};
    CHECK(popped.has_value());
    CHECK(*popped == lap);  // FIFO: we get them out in push order
    CHECK(r.push(next++).has_value());
    CHECK(r.size() == 3);
  }
  // Remaining three are the last three pushed, still in order.
  CHECK(*r.pop() == 20);
  CHECK(*r.pop() == 21);
  CHECK(*r.pop() == 22);
}

TEST_CASE("nexenne::container::ring_buffer power-of-two multi-wrap via mask path") {
  cn::ring_buffer<int, 4> r;  // power-of-two: mask wrap
  int next{0};
  for (int i{0}; i < 4; ++i) {
    r.push(next++);
  }
  for (int lap{0}; lap < 25; ++lap) {
    CHECK(*r.pop() == lap);
    CHECK(r.push(next++).has_value());
  }
  std::vector<int> seen;
  for (int const x : r) {
    seen.push_back(x);
  }
  CHECK(seen == std::vector{25, 26, 27, 28});
}

TEST_CASE("nexenne::container::ring_buffer push_overwrite eviction destroys oldest exactly once") {
  cn::ring_buffer<std::string, 2> r;
  r.push_overwrite("first string long enough to live on the heap not the SSO buffer");
  r.push_overwrite("second string also long enough to escape the small-string buffer");
  // Buffer is full; this evicts "first..." which must be destroyed exactly once
  // (sanitizers catch a leak or double-free of the heap allocation).
  r.push_overwrite("third string likewise heap allocated to provoke any double-free");
  CHECK(r.size() == 2);
  CHECK(r[0] == "second string also long enough to escape the small-string buffer");
  CHECK(r[1] == "third string likewise heap allocated to provoke any double-free");
}

TEST_CASE("nexenne::container::ring_buffer eviction balances construction and destruction") {
  life_counter::reset();
  {
    cn::ring_buffer<life_counter, 3> r;
    for (int i{0}; i < 10; ++i) {
      r.push_overwrite(life_counter{i});  // overflows repeatedly, evicting oldest
      CHECK_FALSE(life_counter::double_free);
    }
    CHECK(r.size() == 3);
    CHECK(life_counter::alive == 3);  // only the live slots remain
    CHECK(r[0].value == 7);
    CHECK(r[1].value == 8);
    CHECK(r[2].value == 9);
  }
  CHECK(life_counter::alive == 0);  // destructor cleaned every slot
  CHECK_FALSE(life_counter::double_free);
  CHECK(life_counter::constructed == life_counter::destroyed);
}

TEST_CASE("nexenne::container::ring_buffer self-aliasing push of an existing element") {
  cn::ring_buffer<std::string, 4> r;
  r.push(std::string{"alpha string long enough to be a genuine heap allocation here"});
  r.push(std::string{"beta string also long enough to avoid the small-string buffer"});
  // push a copy of an element already in the buffer; the source must survive the
  // construction of the new back even if storage were touched.
  CHECK(r.push(r[0]).has_value());
  CHECK(r.emplace(r[1]).has_value());
  CHECK(r.size() == 4);
  CHECK(*r.front() == "alpha string long enough to be a genuine heap allocation here");
  CHECK(r[2] == "alpha string long enough to be a genuine heap allocation here");
  CHECK(r[3] == "beta string also long enough to avoid the small-string buffer");
}

TEST_CASE("nexenne::container::ring_buffer self-aliasing push_overwrite across a wrap") {
  cn::ring_buffer<std::string, 3> r;
  r.push("one string long enough to live on the heap and not be inlined by SSO");
  r.push("two string also long enough to dodge the small-string optimization here");
  r.push("three string likewise heap allocated for the same reason as the rest");
  nexenne::utility::discard(r.pop());  // head advances off zero so subsequent pushes wrap
  r.push("four string heap allocated to keep the buffer at capacity once more");
  // Now full with a non-zero head. push_overwrite a copy of the back, which
  // aliases a live slot while the oldest slot is evicted.
  std::string const expected_back{*r.back()};
  r.push_overwrite(*r.back());
  CHECK(r.size() == 3);
  CHECK(*r.back() == expected_back);
  // And aliasing the element about to be evicted (the front) across the wrap.
  std::string const front_copy{*r.front()};
  r.push_overwrite(*r.front());
  CHECK(*r.back() == front_copy);
}

TEST_CASE("nexenne::container::ring_buffer self copy-assign and self move-assign are no-ops") {
  rb r;
  r.push(1);
  r.push(2);
  r.push(3);

  rb& alias{r};
  r = alias;  // self copy-assign: guard must prevent clearing then reading freed
  CHECK(r.size() == 3);
  CHECK(r[0] == 1);
  CHECK(r[1] == 2);
  CHECK(r[2] == 3);

  r = std::move(alias);  // self move-assign: must not empty the buffer
  CHECK(r.size() == 3);
  CHECK(r[0] == 1);
  CHECK(r[2] == 3);
}

TEST_CASE("nexenne::container::ring_buffer self-swap leaves contents intact") {
  rb r;
  r.push(5);
  r.push(6);
  r.swap(r);  // member self-swap short-circuits
  CHECK(r.size() == 2);
  CHECK(r[0] == 5);
  CHECK(r[1] == 6);
}

TEST_CASE("nexenne::container::ring_buffer moved-from buffer is empty and reusable") {
  rb a;
  a.push(1);
  a.push(2);
  rb b{std::move(a)};
  CHECK(a.empty());
  CHECK(a.size() == 0);
  CHECK(a.front() == nullptr);
  CHECK(a.back() == nullptr);
  // Reuse the moved-from buffer: it behaves like a fresh one.
  CHECK(a.push(10).has_value());
  CHECK(a.push(11).has_value());
  CHECK(a.push(12).has_value());
  CHECK(a.full());
  CHECK(a[0] == 10);
  CHECK(a[2] == 12);

  rb c;
  c.push(7);
  rb d;
  d = std::move(c);  // move-assign source also reusable
  CHECK(c.empty());
  CHECK(c.push(99).has_value());
  CHECK(*c.front() == 99);
}

TEST_CASE("nexenne::container::ring_buffer copies and moves a wrapped std::string buffer") {
  cn::ring_buffer<std::string, 3> a;
  a.push("first string long enough to truly live on the heap and not inline");
  a.push("second string equally long to escape the small-string optimization");
  a.push("third string also heap allocated to keep the element count honest");
  nexenne::utility::discard(a.pop());  // head moves off zero
  a.push("fourth string heap allocated to refill the slot the pop just freed");
  // Logical order now [second, third, fourth] with a non-zero head.

  cn::ring_buffer<std::string, 3> const copy{a};  // copy a wrapped buffer
  CHECK(copy.size() == 3);
  CHECK(copy[0] == "second string equally long to escape the small-string optimization");
  CHECK(copy[2] == "fourth string heap allocated to refill the slot the pop just freed");
  CHECK(a.size() == 3);  // source unchanged by copy

  cn::ring_buffer<std::string, 3> assigned;
  assigned.push("scratch string long enough to be a heap allocation before replace");
  assigned = copy;  // copy-assign over existing contents
  CHECK(assigned.size() == 3);
  CHECK(assigned[1] == "third string also heap allocated to keep the element count honest");

  cn::ring_buffer<std::string, 3> moved{std::move(a)};  // move a wrapped buffer
  CHECK(moved.size() == 3);
  CHECK(moved[0] == "second string equally long to escape the small-string optimization");
  CHECK(moved[2] == "fourth string heap allocated to refill the slot the pop just freed");
  CHECK(a.empty());
}

TEST_CASE("nexenne::container::ring_buffer clear empties and leaves the buffer reusable") {
  cn::ring_buffer<std::string, 3> r;
  r.push("a string long enough to be a real heap allocation for clear testing");
  r.push("b string equally long to keep the destructor doing real heap work here");
  r.clear();
  CHECK(r.empty());
  CHECK(r.size() == 0);
  CHECK(r.front() == nullptr);
  // After clear, head/tail reset to zero and the buffer accepts a full new round.
  CHECK(r.push("x string long enough to live on the heap once more after clear").has_value());
  CHECK(r.push("y string also heap allocated to fill the cleared buffer back up").has_value());
  CHECK(r.push("z string heap allocated to reach capacity in the reused buffer").has_value());
  CHECK(r.full());
  CHECK(*r.front() == "x string long enough to live on the heap once more after clear");
}

TEST_CASE("nexenne::container::ring_buffer const access and const iteration") {
  rb m;
  m.push(1);
  m.push(2);
  m.push(3);
  rb const& r{m};

  // const overloads of front/back/at/operator[]
  CHECK(*r.front() == 1);
  CHECK(*r.back() == 3);
  CHECK(*r.at(1) == 2);
  CHECK(r.at(3) == nullptr);
  CHECK(r[0] == 1);
  CHECK(r[2] == 3);

  static_assert(std::same_as<decltype(r.front()), int const*>);
  static_assert(std::same_as<decltype(r.back()), int const*>);
  static_assert(std::same_as<decltype(r[0]), int const&>);
  static_assert(std::same_as<decltype(r.at(0)), int const*>);

  // const begin/end and cbegin/cend both yield const_iterator over FIFO order.
  std::vector<int> seen;
  for (auto it{r.begin()}; it != r.end(); ++it) {
    seen.push_back(*it);
  }
  CHECK(seen == std::vector{1, 2, 3});

  std::vector<int> seen_c;
  for (auto it{r.cbegin()}; it != r.cend(); ++it) {
    seen_c.push_back(*it);
  }
  CHECK(seen_c == std::vector{1, 2, 3});

  static_assert(std::same_as<decltype(r.begin()), rb::const_iterator>);
  static_assert(std::same_as<decltype(r.cbegin()), rb::const_iterator>);
}

TEST_CASE("nexenne::container::ring_buffer iterator arrow, post-increment, and empty range") {
  cn::ring_buffer<std::pair<int, int>, 3> r;
  r.emplace(1, 10);
  r.emplace(2, 20);

  auto it{r.begin()};
  CHECK(it->first == 1);      // operator->
  auto const previous{it++};  // post-increment returns the old position
  CHECK(previous->first == 1);
  CHECK(it->first == 2);
  ++it;
  CHECK(it == r.end());

  cn::ring_buffer<int, 3> empty;
  CHECK(empty.begin() == empty.end());  // empty range
  CHECK(empty.cbegin() == empty.cend());
  int visited{0};
  for ([[maybe_unused]] int const x : empty) {
    ++visited;
  }
  CHECK(visited == 0);
}

TEST_CASE("nexenne::container::ring_buffer non-const iterator converts to const_iterator") {
  rb r;
  r.push(1);
  r.push(2);
  rb::iterator const mutable_it{r.begin()};
  rb::const_iterator const const_it{mutable_it};  // implicit non-const -> const
  CHECK(*const_it == 1);
  CHECK(const_it == r.cbegin());
}

TEST_CASE("nexenne::container::ring_buffer swap of two wrapped buffers preserves both orders") {
  cn::ring_buffer<std::string, 3> a;
  a.push("a1 string long enough to be a genuine heap allocation in this test");
  a.push("a2 string also long enough to escape the small-string optimization");
  nexenne::utility::discard(a.pop());
  a.push("a3 string heap allocated to leave a with a non-zero head after pop");

  cn::ring_buffer<std::string, 3> b;
  b.push("b1 string long enough to live on the heap for the swap to be visible");

  a.swap(b);  // member swap of two wrapped/partial buffers
  CHECK(a.size() == 1);
  CHECK(*a.front() == "b1 string long enough to live on the heap for the swap to be visible");
  CHECK(b.size() == 2);
  CHECK(b[0] == "a2 string also long enough to escape the small-string optimization");
  CHECK(b[1] == "a3 string heap allocated to leave a with a non-zero head after pop");
}

TEST_CASE("nexenne::container::ring_buffer mutable operator[] and front/back are assignable") {
  rb r;
  r.push(1);
  r.push(2);
  r.push(3);
  r[1] = 20;        // non-const operator[] yields a mutable reference
  *r.front() = 10;  // non-const front
  *r.back() = 30;   // non-const back
  *r.at(1) = 22;    // non-const at re-assigns the same slot
  CHECK(r[0] == 10);
  CHECK(r[1] == 22);
  CHECK(r[2] == 30);
}

// constexpr coverage of push_overwrite eviction, self-aliasing, and copy across
// a wrapped (non-zero head) buffer, all driven at compile time.
static_assert([] {
  cn::ring_buffer<int, 3> r;
  r.push_overwrite(1);
  r.push_overwrite(2);
  r.push_overwrite(3);
  r.push_overwrite(4);  // evicts 1, keeps 2,3,4
  bool ok{r.size() == 3 && r[0] == 2 && r[2] == 4};
  // self-aliasing push_overwrite of the front (the slot about to be evicted)
  r.push_overwrite(r[0]);  // evicts 2, appends a copy of 2 -> 3,4,2
  ok = ok && r[0] == 3 && r[1] == 4 && r[2] == 2;
  return ok;
}());

static_assert([] {
  cn::ring_buffer<int, 3> a;
  a.push(1);
  a.push(2);
  a.push(3);
  nexenne::utility::discard(a.pop());  // head off zero
  a.push(4);                           // logical [2,3,4], non-zero head
  cn::ring_buffer<int, 3> const b{a};  // copy canonicalises head
  bool ok{b.size() == 3 && b[0] == 2 && b[1] == 3 && b[2] == 4};
  cn::ring_buffer<int, 3> c;
  c = std::move(a);  // move-assign at compile time
  ok = ok && c.size() == 3 && c[2] == 4 && a.empty();
  cn::ring_buffer<int, 3> d;
  d.swap(c);  // swap at compile time
  ok = ok && d.size() == 3 && c.empty() && d[0] == 2;
  return ok;
}());

}  // namespace
