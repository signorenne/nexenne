/**
 * @file
 * @brief Tests for nexenne::container::object_pool.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <nexenne/container/object_pool.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;
using pool4 = cn::object_pool<int, 4>;

static_assert(pool4::capacity() == 4);
static_assert(pool4::capacity_v == 4);
// A pool hands out interior pointers, so it is neither copyable nor movable.
static_assert(!std::is_copy_constructible_v<pool4>);
static_assert(!std::is_move_constructible_v<pool4>);

TEST_CASE("nexenne::container::object_pool raw acquire and release tier") {
  pool4 pool;
  CHECK(pool.empty());
  CHECK(pool.size() == 0);
  CHECK_FALSE(pool.full());

  auto const acquired{pool.acquire()};
  REQUIRE(acquired.has_value());
  std::construct_at(*acquired, 42);  // caller owns the lifetime
  CHECK(**acquired == 42);
  CHECK(pool.size() == 1);

  std::destroy_at(*acquired);
  REQUIRE(pool.release(*acquired).has_value());
  CHECK(pool.size() == 0);
}

TEST_CASE("nexenne::container::object_pool emplace and destroy tier") {
  pool4 pool;
  auto const object{pool.emplace(7)};
  REQUIRE(object.has_value());
  CHECK(**object == 7);
  CHECK(pool.size() == 1);
  REQUIRE(pool.destroy(*object).has_value());
  CHECK(pool.size() == 0);
}

TEST_CASE("nexenne::container::object_pool exhaustion and recovery") {
  pool4 pool;
  int* first{nullptr};
  for (int i{0}; i < 4; ++i) {
    auto const object{pool.emplace(i)};
    REQUIRE(object.has_value());
    if (i == 0) {
      first = *object;
    }
  }
  CHECK(pool.full());
  CHECK(pool.acquire().error() == cn::container_error::full);
  CHECK(pool.emplace(99).error() == cn::container_error::full);

  REQUIRE(pool.destroy(first).has_value());  // free one slot
  CHECK_FALSE(pool.full());
  CHECK(pool.emplace(100).has_value());  // a slot is available again
}

TEST_CASE("nexenne::container::object_pool release and destroy validate the pointer") {
  pool4 pool;
  CHECK(pool.release(nullptr).error() == cn::container_error::not_found);
  CHECK(pool.destroy(nullptr).error() == cn::container_error::not_found);
  int foreign{5};
  CHECK(pool.release(&foreign).error() == cn::container_error::out_of_range);
  CHECK(pool.destroy(&foreign).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::object_pool rejects a double release without corrupting the pool") {
  pool4 pool;
  auto const a{pool.emplace(1)};
  auto const b{pool.emplace(2)};
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  REQUIRE(pool.destroy(*a).has_value());
  CHECK(pool.size() == 1);
  // Releasing the same slot again must error, not push it onto the free list a
  // second time (which would later hand the same slot to two acquisitions).
  CHECK(pool.destroy(*a).error() == cn::container_error::out_of_range);
  CHECK(pool.release(*a).error() == cn::container_error::out_of_range);
  CHECK(pool.size() == 1);  // unchanged by the rejected releases

  // The pool stays sound: distinct acquisitions yield distinct slots, and b's
  // slot was never lost.
  auto const c{pool.emplace(3)};
  auto const d{pool.emplace(4)};
  REQUIRE(c.has_value());
  REQUIRE(d.has_value());
  CHECK(*c != *d);
  CHECK(*c != *b);
  CHECK(*d != *b);
  CHECK(pool.size() == 3);  // b, c, d live; the rejected releases lost nothing
}

TEST_CASE("nexenne::container::object_pool rejects an interior pointer") {
  cn::object_pool<long long, 4> pool;
  auto const a{pool.emplace(7)};
  REQUIRE(a.has_value());
  // A pointer inside the slot storage but not at a slot base must be rejected.
  auto* const interior{reinterpret_cast<long long*>(reinterpret_cast<std::byte*>(*a) + 1)};
  CHECK(pool.release(interior).error() == cn::container_error::out_of_range);
  CHECK(pool.size() == 1);
}

TEST_CASE("nexenne::container::object_pool recycles the last freed slot (LIFO)") {
  pool4 pool;
  auto const a{pool.emplace(1)};
  auto const b{pool.emplace(2)};
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  auto* const freed{*b};
  REQUIRE(pool.destroy(*b).has_value());
  auto const c{pool.emplace(3)};
  REQUIRE(c.has_value());
  CHECK(*c == freed);  // the next acquisition reuses the just-freed slot
}

TEST_CASE("nexenne::container::object_pool tracks the high-water mark") {
  pool4 pool;
  auto const a{pool.emplace(1)};
  auto const b{pool.emplace(2)};
  auto const c{pool.emplace(3)};
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  REQUIRE(c.has_value());
  CHECK(pool.high_water_mark() == 3);

  nexenne::utility::discard(pool.destroy(*a));
  nexenne::utility::discard(pool.destroy(*b));
  CHECK(pool.high_water_mark() == 3);  // peak survives releases
  CHECK(pool.size() == 1);
  pool.clear_high_water_mark();
  CHECK(pool.high_water_mark() == 0);
}

namespace {
struct tracked {
  int* live;
  int value;

  tracked(int* l, int v) noexcept : live{l}, value{v} {
    ++*live;
  }

  ~tracked() {
    --*live;
  }
};

struct immovable {
  int value;

  explicit immovable(int v) noexcept : value{v} {}

  immovable(immovable const&) = delete;
  immovable(immovable&&) = delete;
  auto operator=(immovable const&) -> immovable& = delete;
  auto operator=(immovable&&) -> immovable& = delete;
};
}  // namespace

TEST_CASE("nexenne::container::object_pool destroy runs the destructor") {
  int live{0};
  {
    cn::object_pool<tracked, 4> pool;
    auto const object{pool.emplace(&live, 10)};
    REQUIRE(object.has_value());
    CHECK(live == 1);  // emplace constructed in place
    REQUIRE(pool.destroy(*object).has_value());
    CHECK(live == 0);  // destroy ran the destructor
  }
  CHECK(live == 0);
}

TEST_CASE("nexenne::container::object_pool holds an immovable type") {
  cn::object_pool<immovable, 2> pool;  // never moved or copied by the pool
  auto const object{pool.emplace(5)};
  REQUIRE(object.has_value());
  CHECK((*object)->value == 5);
  REQUIRE(pool.destroy(*object).has_value());
}

TEST_CASE("nexenne::container::object_pool acquiring every slot yields distinct storage") {
  pool4 pool;
  std::vector<int*> handles;
  for (int i{0}; i < 4; ++i) {
    auto const got{pool.acquire()};
    REQUIRE(got.has_value());
    for (auto* const prior : handles) {
      CHECK(prior != *got);  // every slot is a distinct address
    }
    handles.push_back(*got);
  }
  CHECK(pool.full());
  CHECK(pool.size() == 4);
  for (auto* const h : handles) {  // raw tier: no T was constructed, just release
    REQUIRE(pool.release(h).has_value());
  }
  CHECK(pool.empty());
}

TEST_CASE("nexenne::container::object_pool const inspection accessors") {
  pool4 pool;
  nexenne::utility::discard(pool.emplace(1));
  auto const& view{pool};
  CHECK(view.size() == 1);
  CHECK_FALSE(view.empty());
  CHECK_FALSE(view.full());
  CHECK(view.high_water_mark() == 1);
  CHECK(view.capacity() == 4);
  CHECK(view.max_size() == 4);
}

TEST_CASE("nexenne::container::object_pool rejects an address above the slot storage") {
  pool4 pool;
  auto const a{pool.emplace(0)};
  REQUIRE(a.has_value());
  // An address well above the pool's storage is foreign; the byte bounds check in
  // acquired_index must reject it rather than form an out-of-range slot index. Two
  // separate stack ints bracket the pool's storage, so one of them is guaranteed
  // outside [base, base + N*sizeof(slot)); both must be rejected regardless.
  int high_foreign{1};
  int low_foreign{2};
  CHECK(pool.release(&high_foreign).error() == cn::container_error::out_of_range);
  CHECK(pool.destroy(&low_foreign).error() == cn::container_error::out_of_range);
  CHECK(pool.size() == 1);
}

TEST_CASE("nexenne::container::object_pool single-slot pool boundary") {
  cn::object_pool<int, 1> pool;
  CHECK(pool.capacity() == 1);
  CHECK(pool.empty());
  auto const a{pool.emplace(5)};
  REQUIRE(a.has_value());
  CHECK(pool.full());
  CHECK(pool.emplace(6).error() == cn::container_error::full);
  REQUIRE(pool.destroy(*a).has_value());
  CHECK(pool.empty());
  auto const b{pool.emplace(7)};  // the one slot recycles
  REQUIRE(b.has_value());
  CHECK(**b == 7);
}

TEST_CASE("nexenne::container::object_pool high-water mark tracks the running peak") {
  pool4 pool;
  auto const a{pool.emplace(1)};
  auto const b{pool.emplace(2)};
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(pool.high_water_mark() == 2);
  REQUIRE(pool.destroy(*a).has_value());  // dip to 1
  CHECK(pool.high_water_mark() == 2);     // peak unchanged by the release
  auto const c{pool.emplace(3)};
  auto const d{pool.emplace(4)};  // climb to 3
  REQUIRE(c.has_value());
  REQUIRE(d.has_value());
  CHECK(pool.high_water_mark() == 3);  // new peak recorded
  pool.clear_high_water_mark();
  CHECK(pool.high_water_mark() == 0);
  CHECK(pool.size() == 3);  // live slots untouched by the reset
}

TEST_CASE("nexenne::container::object_pool holds a non-trivial std::string element") {
  int constexpr count{4};
  cn::object_pool<std::string, count> pool;
  std::vector<std::string*> objects;
  for (int i{0}; i < count; ++i) {
    auto const got{pool.emplace(40, static_cast<char>('a' + i))};  // heap-backed string
    REQUIRE(got.has_value());
    CHECK((*got)->size() == 40);
    objects.push_back(*got);
  }
  CHECK(pool.full());
  // destroy must run ~std::string on each, freeing the heap buffer (LSan check).
  for (auto* const obj : objects) {
    REQUIRE(pool.destroy(obj).has_value());
  }
  CHECK(pool.empty());

  // Recycle and leave one live: its destructor must NOT run at pool destruction,
  // so destroy it explicitly to avoid the documented leak.
  auto const live{pool.emplace(64, 'z')};
  REQUIRE(live.has_value());
  CHECK((*live)->size() == 64);
  REQUIRE(pool.destroy(*live).has_value());
}

}  // namespace
