/**
 * @file
 * @brief Tests for nexenne::container::stable_vector.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <type_traits>
#include <utility>

#include <nexenne/container/stable_vector.hpp>

namespace {

namespace cn = nexenne::container;
using sv = cn::stable_vector<int, 2>;  // small chunk to force multiple chunks

static_assert(sv::chunk_size == 2);
static_assert(std::random_access_iterator<sv::iterator>);
static_assert(std::random_access_iterator<sv::const_iterator>);

// Type-level guarantees checkable at compile time. The container itself is not
// constexpr-constructible (it owns std::unique_ptr<chunk> over a union slot), so
// value-level constexpr evaluation is not exercisable; these assert the traits
// the public surface promises instead.
static_assert(std::is_nothrow_default_constructible_v<sv>);
static_assert(std::is_nothrow_move_constructible_v<sv>);
static_assert(noexcept(std::declval<sv const&>().size()));
static_assert(noexcept(std::declval<sv const&>().empty()));
static_assert(sv::max_size() == std::numeric_limits<sv::size_type>::max());
static_assert(std::is_same_v<sv::value_type, int>);
static_assert(std::is_same_v<sv::reference, int&>);
static_assert(std::is_same_v<sv::const_reference, int const&>);
// A non-const iterator is convertible to a const_iterator, but not the reverse.
static_assert(std::is_convertible_v<sv::iterator, sv::const_iterator>);
static_assert(!std::is_convertible_v<sv::const_iterator, sv::iterator>);

TEST_CASE("nexenne::container::stable_vector addresses survive growth across chunks") {
  sv v;
  int* const p0{v.push_back(10)};
  int* const p1{v.push_back(20)};
  int* const p2{v.push_back(30)};  // crosses into a second chunk
  for (int i{0}; i < 50; ++i) {
    v.push_back(i);  // allocates many more chunks
  }
  CHECK(*p0 == 10);  // old pointers still valid and unchanged
  CHECK(*p1 == 20);
  CHECK(*p2 == 30);
  CHECK(p0 == &v[0]);  // address matches the logical slot
  CHECK(p2 == &v[2]);
  CHECK(v.size() == 53);
}

TEST_CASE("nexenne::container::stable_vector push/emplace/pop") {
  sv v;
  CHECK(*v.push_back(1) == 1);
  auto& slot{v.emplace_back(2)};
  CHECK(slot == 2);
  slot = 9;
  CHECK(v[1] == 9);
  CHECK(v.pop_back().has_value());
  CHECK(v.size() == 1);
  CHECK(v.pop_back().has_value());
  CHECK(v.pop_back().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::stable_vector at/front/back") {
  sv v{10, 20, 30};
  CHECK(*v.at(2) == 30);
  CHECK(v.at(3) == nullptr);
  CHECK(*v.front() == 10);
  CHECK(*v.back() == 30);

  sv empty;
  CHECK(empty.front() == nullptr);
  CHECK(empty.back() == nullptr);
}

TEST_CASE("nexenne::container::stable_vector random-access iteration") {
  sv v{1, 2, 3, 4, 5};
  CHECK(std::ranges::equal(v, std::array{1, 2, 3, 4, 5}));
  auto const it{v.begin()};
  CHECK(it[3] == 4);  // random access across a chunk boundary
  CHECK(*(it + 2) == 3);
  CHECK((v.end() - v.begin()) == 5);

  int sum{0};
  for (int const x : v) {
    sum += x;
  }
  CHECK(sum == 15);
}

TEST_CASE("nexenne::container::stable_vector copy is deep and independent") {
  sv a{1, 2, 3};
  sv b{a};
  CHECK(b == a);
  a[0] = 99;
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::stable_vector move steals chunks and preserves addresses") {
  sv a{1, 2, 3, 4, 5};
  int* const front{&a[0]};
  sv b{std::move(a)};
  CHECK(b.size() == 5);
  CHECK(&b[0] == front);  // stole the chunk, address preserved
  CHECK(a.empty());
}

TEST_CASE("nexenne::container::stable_vector copy-and-swap assignment") {
  sv const a{1, 2, 3};
  sv b;
  b = a;
  CHECK(b == a);

  sv source{4, 5};
  sv c;
  c = std::move(source);
  CHECK(c.size() == 2);
  CHECK(source.empty());
}

TEST_CASE("nexenne::container::stable_vector swap is O(1) and keeps addresses") {
  sv a{1, 2};
  int* const front_a{&a[0]};
  sv b{7, 8, 9};
  swap(a, b);
  CHECK(a.size() == 3);
  CHECK(b.size() == 2);
  CHECK(&b[0] == front_a);  // a's chunk now lives in b, address preserved
}

TEST_CASE("nexenne::container::stable_vector reserve, shrink_to_fit, capacity") {
  sv v;
  v.reserve(10);  // ceil(10 / 2) = 5 chunks
  CHECK(v.capacity() >= 10);
  CHECK(v.chunk_count() == 5);
  v.push_back(1);
  v.push_back(2);
  v.shrink_to_fit();  // size 2 fits in one chunk
  CHECK(v.chunk_count() == 1);
  CHECK(v.size() == 2);
}

TEST_CASE("nexenne::container::stable_vector comparison") {
  CHECK(sv{1, 2, 3} == sv{1, 2, 3});
  CHECK(sv{1, 2} != sv{1, 2, 3});
  CHECK(sv{1, 2, 3} < sv{1, 2, 4});
}

TEST_CASE("nexenne::container::stable_vector holds a move-only type") {
  cn::stable_vector<std::unique_ptr<int>, 2> v;
  for (int i{0}; i < 5; ++i) {
    v.push_back(std::make_unique<int>(i));
  }
  CHECK(v.size() == 5);
  CHECK(*v[4] == 4);
}

TEST_CASE("nexenne::container::stable_vector destroys its elements") {
  auto tracker{std::make_shared<int>(0)};
  {
    cn::stable_vector<std::shared_ptr<int>, 2> v;
    for (int i{0}; i < 5; ++i) {
      v.push_back(tracker);
    }
    CHECK(tracker.use_count() == 6);  // tracker + five stored
  }
  CHECK(tracker.use_count() == 1);  // destructor destroyed all
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

TEST_CASE("nexenne::container::stable_vector push_back picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::stable_vector<move_counter, 4> v;
  move_counter c{&copies, &moves};

  v.push_back(c);  // lvalue: one copy, no move
  CHECK(copies == 1);
  CHECK(moves == 0);

  v.push_back(std::move(c));  // rvalue: one move
  CHECK(copies == 1);
  CHECK(moves == 1);
}

TEST_CASE("nexenne::container::stable_vector default is empty") {
  sv v;
  CHECK(v.empty());
  CHECK(v.size() == 0);
  CHECK(v.chunk_count() == 0);
  CHECK(v.capacity() == 0);
  CHECK(v.begin() == v.end());
  CHECK(v.front() == nullptr);
  CHECK(v.back() == nullptr);
  CHECK(v.at(0) == nullptr);
  CHECK_FALSE(v.pop_back().has_value());
}

TEST_CASE("nexenne::container::stable_vector single element") {
  sv v;
  int* const p{v.push_back(42)};
  CHECK(v.size() == 1);
  CHECK_FALSE(v.empty());
  CHECK(p == &v[0]);
  CHECK(p == v.front());
  CHECK(p == v.back());
  CHECK(p == v.at(0));
  CHECK(std::next(v.begin()) == v.end());
}

TEST_CASE("nexenne::container::stable_vector chunk-boundary capacity and counts") {
  sv v;  // ChunkSize == 2
  CHECK(v.chunk_count() == 0);

  v.push_back(1);  // first element allocates the first chunk
  CHECK(v.chunk_count() == 1);
  CHECK(v.capacity() == 2);

  v.push_back(2);  // exactly fills the first chunk, no new chunk yet
  CHECK(v.chunk_count() == 1);
  CHECK(v.capacity() == 2);

  v.push_back(3);  // growth past the chunk boundary allocates a second chunk
  CHECK(v.chunk_count() == 2);
  CHECK(v.capacity() == 4);
}

TEST_CASE("nexenne::container::stable_vector pop_back destroys and respects boundaries") {
  auto tracker{std::make_shared<int>(0)};
  cn::stable_vector<std::shared_ptr<int>, 2> v;
  for (int i{0}; i < 3; ++i) {  // spans two chunks
    v.push_back(tracker);
  }
  CHECK(tracker.use_count() == 4);  // tracker + three stored

  CHECK(v.pop_back().has_value());  // pops the lone element of chunk two
  CHECK(tracker.use_count() == 3);
  CHECK(v.size() == 2);

  CHECK(v.pop_back().has_value());
  CHECK(tracker.use_count() == 2);

  CHECK(v.pop_back().has_value());
  CHECK(tracker.use_count() == 1);  // all stored copies destroyed
  CHECK(v.empty());

  CHECK(v.pop_back().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::stable_vector clear destroys elements and keeps capacity") {
  auto tracker{std::make_shared<int>(0)};
  cn::stable_vector<std::shared_ptr<int>, 2> v;
  for (int i{0}; i < 5; ++i) {
    v.push_back(tracker);
  }
  auto const chunks_before{v.chunk_count()};
  CHECK(tracker.use_count() == 6);

  v.clear();
  CHECK(v.empty());
  CHECK(v.size() == 0);
  CHECK(tracker.use_count() == 1);          // clear destroyed every element
  CHECK(v.chunk_count() == chunks_before);  // chunk storage retained

  v.push_back(tracker);  // reusable after clear
  CHECK(v.size() == 1);
  CHECK(tracker.use_count() == 2);
}

TEST_CASE("nexenne::container::stable_vector addresses stable through pop and refill") {
  sv v{1, 2, 3, 4};
  int* const p0{&v[0]};
  int* const p1{&v[1]};
  CHECK(v.pop_back().has_value());  // drops index 3
  CHECK(v.pop_back().has_value());  // drops index 2
  CHECK(p0 == &v[0]);               // survivors keep their addresses
  CHECK(p1 == &v[1]);
  v.push_back(30);  // reuses the freed slot in chunk two
  v.push_back(40);
  CHECK(p0 == &v[0]);
  CHECK(p1 == &v[1]);
  CHECK(*p0 == 1);
  CHECK(*p1 == 2);
  CHECK(v[2] == 30);
  CHECK(v[3] == 40);
}

TEST_CASE("nexenne::container::stable_vector addresses stable through reserve and shrink") {
  sv v{1, 2, 3};
  int* const p0{&v[0]};
  int* const p2{&v[2]};
  v.reserve(1000);  // forces the chunk-pointer vector to reallocate
  CHECK(p0 == &v[0]);
  CHECK(p2 == &v[2]);
  v.shrink_to_fit();  // releases the surplus empty chunks
  CHECK(p0 == &v[0]);
  CHECK(p2 == &v[2]);
  CHECK(*p0 == 1);
  CHECK(*p2 == 3);
}

TEST_CASE("nexenne::container::stable_vector many addresses survive heavy growth") {
  cn::stable_vector<int, 4> v;
  std::array<int*, 20> saved{};
  for (int i{0}; i < 20; ++i) {
    saved[static_cast<std::size_t>(i)] = v.push_back(i);  // spans five chunks
  }
  for (int j{0}; j < 500; ++j) {
    v.push_back(1000 + j);  // force far more chunk allocations
  }
  for (int i{0}; i < 20; ++i) {
    auto const k{static_cast<std::size_t>(i)};
    CHECK(saved[k] == &v[k]);  // every saved pointer still maps to its slot
    CHECK(*saved[k] == i);     // and still reads its original value
  }
  CHECK(v.size() == 520);
}

TEST_CASE("nexenne::container::stable_vector self-aliasing push_back of own element") {
  sv v{5, 6, 7};
  v.push_back(v[0]);  // copy of an existing element (may trigger growth)
  v.push_back(v[1]);
  CHECK(v.size() == 5);
  CHECK(v[3] == 5);
  CHECK(v[4] == 6);
  CHECK(v[0] == 5);  // source untouched
  CHECK(v[1] == 6);
}

TEST_CASE("nexenne::container::stable_vector self-aliasing emplace_back of own element") {
  sv v{1, 2};
  v.emplace_back(v[1]);  // emplace from a reference into the same container
  CHECK(v.size() == 3);
  CHECK(v[2] == 2);
  CHECK(v[1] == 2);
}

TEST_CASE("nexenne::container::stable_vector self copy-assignment is a no-op") {
  sv v{1, 2, 3};
  sv& alias{v};
  v = alias;  // copy-and-swap copies into a temporary first, so this is safe
  CHECK(v.size() == 3);
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));
}

TEST_CASE("nexenne::container::stable_vector self move-assignment leaves it valid") {
  sv v{1, 2, 3};
  auto* const self{&v};
  v = std::move(*self);  // self move-assign (by value: feeds a temporary), past -Wself-move
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));
  v.push_back(4);  // still usable
  CHECK(v.size() == 4);
}

TEST_CASE("nexenne::container::stable_vector moved-from is empty and reusable") {
  sv a{1, 2, 3};
  sv b{std::move(a)};
  CHECK(a.empty());  // moved-from is left empty
  CHECK(a.size() == 0);
  CHECK(a.begin() == a.end());
  a.push_back(9);  // and is reusable
  a.push_back(8);
  CHECK(a.size() == 2);
  CHECK(a[0] == 9);
  CHECK(a[1] == 8);
  CHECK(b.size() == 3);  // target intact
}

TEST_CASE("nexenne::container::stable_vector moved-from via assignment is reusable") {
  sv source{1, 2, 3, 4, 5};
  sv dest;
  dest = std::move(source);
  CHECK(dest.size() == 5);
  CHECK(source.empty());
  source.push_back(7);  // reusable after being moved out of
  CHECK(source.size() == 1);
  CHECK(source[0] == 7);
}

TEST_CASE("nexenne::container::stable_vector copy of empty and self-equality") {
  sv empty;
  sv copy{empty};
  CHECK(copy.empty());
  CHECK(copy == empty);
  CHECK(empty == empty);
}

TEST_CASE("nexenne::container::stable_vector const access overloads") {
  sv const v{10, 20, 30};
  CHECK(v[1] == 20);      // const operator[]
  CHECK(*v.at(2) == 30);  // const at
  CHECK(v.at(5) == nullptr);
  CHECK(*v.front() == 10);  // const front
  CHECK(*v.back() == 30);   // const back

  static_assert(std::is_same_v<decltype(v[0]), int const&>);
  static_assert(std::is_same_v<decltype(v.at(0)), int const*>);
  static_assert(std::is_same_v<decltype(v.front()), int const*>);
  static_assert(std::is_same_v<decltype(v.back()), int const*>);

  sv const empty;
  CHECK(empty.front() == nullptr);
  CHECK(empty.back() == nullptr);
}

TEST_CASE("nexenne::container::stable_vector mutation through operator[] and iterator") {
  sv v{1, 2, 3, 4};
  v[2] = 30;
  CHECK(v[2] == 30);
  *(v.begin() + 1) = 20;
  CHECK(v[1] == 20);
  for (int& x : v) {
    x += 1;
  }
  CHECK(std::ranges::equal(v, std::array{2, 21, 31, 5}));
}

TEST_CASE("nexenne::container::stable_vector iterator decrement and full arithmetic") {
  sv v{1, 2, 3, 4, 5};
  auto it{v.end()};
  --it;
  CHECK(*it == 5);
  it--;
  CHECK(*it == 4);
  it -= 2;
  CHECK(*it == 2);
  it += 3;
  CHECK(*it == 5);
  CHECK(*(it - 4) == 1);  // crosses several chunk boundaries
  CHECK(*(2 + v.begin()) == 3);

  auto post{v.begin()};
  auto const pre_value{*(post++)};
  CHECK(pre_value == 1);
  CHECK(*post == 2);
}

TEST_CASE("nexenne::container::stable_vector iterator comparison and ordering") {
  sv v{1, 2, 3};
  auto const b{v.begin()};
  auto const e{v.end()};
  CHECK(b < e);
  CHECK(b <= e);
  CHECK(e > b);
  CHECK(e >= b);
  CHECK(b != e);
  CHECK((b + 3) == e);
  CHECK((e - b) == 3);
}

TEST_CASE("nexenne::container::stable_vector const_iterator conversion and cbegin/cend") {
  sv v{1, 2, 3};
  sv::const_iterator ci{v.begin()};  // implicit non-const -> const conversion
  CHECK(*ci == 1);
  CHECK(v.cbegin() == v.begin());  // mixed-constness equality
  CHECK(v.cend() == v.end());
  CHECK((v.cend() - v.cbegin()) == 3);

  sv const& cref{v};
  CHECK(std::ranges::equal(cref, std::array{1, 2, 3}));  // const begin/end
  static_assert(std::is_same_v<decltype(*v.cbegin()), int const&>);
}

TEST_CASE("nexenne::container::stable_vector reverse iteration") {
  sv v{1, 2, 3, 4, 5};
  CHECK(std::ranges::equal(std::ranges::subrange(v.rbegin(), v.rend()), std::array{5, 4, 3, 2, 1}));

  sv const& cref{v};
  CHECK(
    std::ranges::equal(std::ranges::subrange(cref.rbegin(), cref.rend()), std::array{5, 4, 3, 2, 1})
  );
  CHECK(v.crbegin() == cref.rbegin());
  CHECK((v.crend() - v.crbegin()) == 5);

  *v.rbegin() = 50;  // mutate through reverse iterator
  CHECK(v.back() != nullptr);
  CHECK(*v.back() == 50);
}

TEST_CASE("nexenne::container::stable_vector works with std algorithms across chunks") {
  sv v{5, 3, 1, 4, 2};
  std::sort(v.begin(), v.end());  // random-access mutation across chunks
  CHECK(std::ranges::equal(v, std::array{1, 2, 3, 4, 5}));

  auto const found{std::find(v.begin(), v.end(), 4)};
  CHECK(found != v.end());
  CHECK((found - v.begin()) == 3);

  auto const total{std::accumulate(v.begin(), v.end(), 0)};
  CHECK(total == 15);
}

TEST_CASE("nexenne::container::stable_vector three-way and equality edge cases") {
  CHECK((sv{1, 2, 3} <=> sv{1, 2, 3}) == std::strong_ordering::equal);
  CHECK((sv{1, 2} <=> sv{1, 2, 3}) == std::strong_ordering::less);  // prefix is less
  CHECK((sv{1, 2, 4} <=> sv{1, 2, 3}) == std::strong_ordering::greater);
  CHECK(sv{1, 2, 4} > sv{1, 2, 3});
  CHECK(sv{} == sv{});  // two empties compare equal
  CHECK_FALSE(sv{1} == sv{2});
  CHECK((sv{} <=> sv{1}) == std::strong_ordering::less);
}

TEST_CASE("nexenne::container::stable_vector holds a non-trivial std::string") {
  cn::stable_vector<std::string, 2> v;
  std::string const long_a(64, 'a');  // heap-allocated, defeats SSO
  std::string const long_b(64, 'b');
  std::string* const pa{v.push_back(long_a)};
  v.push_back(long_b);
  v.emplace_back(32, 'c');
  for (int i{0}; i < 10; ++i) {  // force growth across chunks
    v.push_back(std::to_string(i));
  }
  CHECK(*pa == long_a);  // address stable, value intact after growth
  CHECK(pa == &v[0]);
  CHECK(v[1] == long_b);
  CHECK(v[2] == std::string(32, 'c'));
  CHECK(v.back() != nullptr);
  CHECK(*v.back() == "9");
  CHECK(v.begin()->size() == 64);  // operator-> reaches the element's members

  cn::stable_vector<std::string, 2> copy{v};  // deep copy of non-trivial type
  CHECK(copy == v);
  CHECK(&copy[0] != &v[0]);  // copy's addresses are independent
  v[0] = "changed";
  CHECK(copy[0] == long_a);
}

TEST_CASE("nexenne::container::stable_vector move-only addresses survive growth") {
  cn::stable_vector<std::unique_ptr<int>, 2> v;
  std::array<int*, 4> targets{};
  for (int i{0}; i < 4; ++i) {
    auto* const slot{v.push_back(std::make_unique<int>(i))};
    targets[static_cast<std::size_t>(i)] = slot->get();
  }
  for (int i{0}; i < 30; ++i) {  // heavy growth
    v.push_back(std::make_unique<int>(100 + i));
  }
  for (int i{0}; i < 4; ++i) {
    auto const k{static_cast<std::size_t>(i)};
    CHECK(v[k].get() == targets[k]);  // the held pointer is untouched
    CHECK(*v[k] == i);
  }

  cn::stable_vector<std::unique_ptr<int>, 2> moved{std::move(v)};
  CHECK(moved.size() == 34);
  CHECK(v.empty());
  CHECK(*moved[0] == 0);
}

TEST_CASE("nexenne::container::stable_vector emplace_back returns a usable reference") {
  sv v;
  int& r0{v.emplace_back(7)};
  r0 = 70;
  CHECK(v[0] == 70);
  int& r1{v.emplace_back(8)};  // possibly in a new chunk
  CHECK(&r1 == &v[1]);
  CHECK(&r0 == &v[0]);  // earlier reference unaffected by growth
}

TEST_CASE("nexenne::container::stable_vector shrink_to_fit on empty and reserve growth") {
  sv v;
  v.shrink_to_fit();  // no-op on empty
  CHECK(v.chunk_count() == 0);

  v.reserve(0);  // ceil(0/2) = 0 chunks
  CHECK(v.chunk_count() == 0);

  v.reserve(5);  // ceil(5/2) = 3 chunks
  CHECK(v.chunk_count() == 3);
  CHECK(v.size() == 0);  // reserve does not change size
  CHECK(v.empty());

  v.reserve(3);  // smaller request never shrinks
  CHECK(v.chunk_count() == 3);
}

}  // namespace
