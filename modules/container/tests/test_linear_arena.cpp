/**
 * @file
 * @brief Tests for nexenne::container::linear_arena.
 */

#include <doctest/doctest.h>

#include <cstdint>

#include <nexenne/container/linear_arena.hpp>

namespace {

namespace cn = nexenne::container;
using arena = cn::linear_arena<256>;

static_assert(arena::capacity() == 256);
static_assert(arena::buffer_size == 256);

[[nodiscard]] auto address_of(void const* p) noexcept -> std::uintptr_t {
  return reinterpret_cast<std::uintptr_t>(p);
}

TEST_CASE("nexenne::container::linear_arena bumps and respects alignment") {
  arena a;
  CHECK(a.empty());
  CHECK(a.bytes_available() == 256);

  auto const p1{a.allocate(10, 8)};
  REQUIRE(p1.has_value());
  CHECK(address_of(*p1) % 8 == 0);
  CHECK(a.bytes_used() >= 10);

  auto const p2{a.allocate(4, 16)};
  REQUIRE(p2.has_value());
  CHECK(address_of(*p2) % 16 == 0);
  CHECK(*p2 != *p1);  // distinct blocks
}

TEST_CASE("nexenne::container::linear_arena typed allocate and emplace") {
  arena a;
  auto const block{a.allocate<int>(4)};
  REQUIRE(block.has_value());
  CHECK(address_of(*block) % alignof(int) == 0);
  (*block)[0] = 42;
  CHECK((*block)[0] == 42);

  auto const object{a.emplace<int>(7)};
  REQUIRE(object.has_value());
  CHECK(**object == 7);
}

TEST_CASE("nexenne::container::linear_arena reports full when out of room") {
  cn::linear_arena<16> small;
  CHECK(small.allocate(16, 1).has_value());
  CHECK(small.allocate(1, 1).error() == cn::container_error::full);
}

TEST_CASE("nexenne::container::linear_arena reset releases everything") {
  arena a;
  CHECK(a.allocate(100, 1).has_value());
  CHECK(a.bytes_used() == 100);
  a.reset();
  CHECK(a.empty());
  CHECK(a.bytes_used() == 0);
}

TEST_CASE("nexenne::container::linear_arena rewind_to releases back to a checkpoint") {
  arena a;
  CHECK(a.allocate(20, 1).has_value());
  auto const checkpoint{a.bytes_used()};
  CHECK(a.allocate(50, 1).has_value());
  CHECK(a.bytes_used() == 70);

  a.rewind_to(checkpoint);
  CHECK(a.bytes_used() == 20);
  a.rewind_to(1000);  // above the current offset, ignored
  CHECK(a.bytes_used() == 20);
}

TEST_CASE("nexenne::container::linear_arena tracks the high-water mark across resets") {
  arena a;
  CHECK(a.allocate(100, 1).has_value());
  CHECK(a.allocate(50, 1).has_value());  // peak 150
  a.reset();
  CHECK(a.allocate(30, 1).has_value());
  CHECK(a.high_water_mark() == 150);  // peak survives reset
  a.clear_high_water_mark();
  CHECK(a.high_water_mark() == 0);
}

}  // namespace
