/**
 * @file
 * @brief Tests for nexenne::container::object_pool.
 */

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>

#include <nexenne/container/object_pool.hpp>

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

  static_cast<void>(pool.destroy(*a));
  static_cast<void>(pool.destroy(*b));
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

}  // namespace
