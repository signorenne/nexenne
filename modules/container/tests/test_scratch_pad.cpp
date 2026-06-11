/**
 * @file
 * @brief Tests for nexenne::container::scratch_pad.
 */

#include <doctest/doctest.h>

#include <type_traits>

#include <nexenne/container/linear_arena.hpp>
#include <nexenne/container/scratch_pad.hpp>

namespace {

namespace cn = nexenne::container;
using arena_t = cn::linear_arena<256>;

// A scratch_pad is a scope-bound guard: non-copyable and non-movable.
static_assert(!std::is_copy_constructible_v<cn::scratch_pad<arena_t>>);
static_assert(!std::is_move_constructible_v<cn::scratch_pad<arena_t>>);

TEST_CASE("nexenne::container::scratch_pad rewinds the arena on scope exit") {
  arena_t a;
  CHECK(a.allocate(20, 1).has_value());  // long-lived
  auto const before{a.bytes_used()};
  {
    cn::scratch_pad scratch{a};  // CTAD deduces the arena type
    CHECK(scratch.saved_offset() == before);
    CHECK(scratch.allocate(50, 1).has_value());
    CHECK(a.bytes_used() == before + 50);
  }
  CHECK(a.bytes_used() == before);  // released on scope exit
}

TEST_CASE("nexenne::container::scratch_pad forwards typed allocate and emplace") {
  arena_t a;
  cn::scratch_pad scratch{a};
  auto const block{scratch.allocate<int>(4)};
  REQUIRE(block.has_value());
  auto const object{scratch.emplace<int>(9)};
  REQUIRE(object.has_value());
  CHECK(**object == 9);
  CHECK(scratch.arena().bytes_used() > 0);
}

TEST_CASE("nexenne::container::scratch_pad nests") {
  arena_t a;
  {
    cn::scratch_pad outer{a};
    CHECK(outer.allocate(30, 1).has_value());
    auto const mid{a.bytes_used()};
    {
      cn::scratch_pad inner{a};
      CHECK(inner.allocate(40, 1).has_value());
      CHECK(a.bytes_used() == mid + 40);
    }
    CHECK(a.bytes_used() == mid);  // inner rewound, outer's allocation intact
  }
  CHECK(a.bytes_used() == 0);  // outer rewound
}

}  // namespace
