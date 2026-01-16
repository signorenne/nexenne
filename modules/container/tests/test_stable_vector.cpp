/**
 * @file
 * @brief Tests for nexenne::container::stable_vector.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <utility>

#include <nexenne/container/stable_vector.hpp>

namespace {

namespace cn = nexenne::container;
using sv = cn::stable_vector<int, 2>;  // small chunk to force multiple chunks

static_assert(sv::chunk_size == 2);
static_assert(std::random_access_iterator<sv::iterator>);
static_assert(std::random_access_iterator<sv::const_iterator>);

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

}  // namespace
