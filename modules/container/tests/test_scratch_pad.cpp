/**
 * @file
 * @brief Tests for nexenne::container::scratch_pad.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include <nexenne/container/error.hpp>
#include <nexenne/container/linear_arena.hpp>
#include <nexenne/container/scratch_pad.hpp>

namespace {

namespace cn = nexenne::container;
using arena_t = cn::linear_arena<256>;

// A scratch_pad is a scope-bound guard: non-copyable and non-movable.
static_assert(!std::is_copy_constructible_v<cn::scratch_pad<arena_t>>);
static_assert(!std::is_move_constructible_v<cn::scratch_pad<arena_t>>);
static_assert(!std::is_copy_assignable_v<cn::scratch_pad<arena_t>>);
static_assert(!std::is_move_assignable_v<cn::scratch_pad<arena_t>>);

// The CTAD guide deduces the arena type, and the wrapped types are exposed.
static_assert(std::is_same_v<cn::scratch_pad<arena_t>::arena_type, arena_t>);
static_assert(std::is_same_v<cn::scratch_pad<arena_t>::size_type, arena_t::size_type>);

// A linear_arena satisfies the checkpointable_arena concept that the
// scratch_pad requires of its template argument.
static_assert(cn::checkpointable_arena<arena_t>);

// Returns the integer address of a pointer for alignment checks.
[[nodiscard]] auto address_of(void const* const p) noexcept -> std::uintptr_t {
  return reinterpret_cast<std::uintptr_t>(p);
}

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

TEST_CASE("nexenne::container::scratch_pad allocate honours every power-of-two alignment") {
  // The arena only guarantees alignments up to alignof(std::max_align_t); a
  // larger request cannot be satisfied and is a precondition violation, so the
  // sweep stops there.
  arena_t a;
  cn::scratch_pad scratch{a};
  for (arena_t::size_type alignment{1}; alignment <= alignof(std::max_align_t); alignment *= 2) {
    auto const block{scratch.allocate(8, alignment)};
    REQUIRE(block.has_value());
    CHECK(address_of(*block) % alignment == 0);
  }
}

TEST_CASE("nexenne::container::scratch_pad typed allocate yields storage aligned for the type") {
  struct alignas(alignof(std::max_align_t)) over_aligned {
    std::byte payload[alignof(std::max_align_t)];
  };

  static_assert(alignof(over_aligned) == alignof(std::max_align_t));

  arena_t a;
  cn::scratch_pad scratch{a};
  auto const block{scratch.allocate<over_aligned>(2)};
  REQUIRE(block.has_value());
  CHECK(address_of(*block) % alignof(over_aligned) == 0);
}

TEST_CASE("nexenne::container::scratch_pad counts alignment padding in the arena's used bytes") {
  arena_t a;
  cn::scratch_pad scratch{a};
  // Push the bump offset to an odd position, then demand a 16-byte alignment so
  // the next allocation must skip padding bytes that still count as used.
  REQUIRE(scratch.allocate(1, 1).has_value());
  CHECK(a.bytes_used() == 1);
  REQUIRE(scratch.allocate(8, 16).has_value());
  CHECK(a.bytes_used() == 16 + 8);  // 15 padding bytes plus the 8 requested
}

TEST_CASE("nexenne::container::scratch_pad reports full at exact capacity and beyond") {
  arena_t a;
  cn::scratch_pad scratch{a};
  // Fill to the brim, then one more byte must fail without disturbing state.
  REQUIRE(scratch.allocate(arena_t::capacity(), 1).has_value());
  CHECK(a.bytes_used() == arena_t::capacity());

  auto const overflow{scratch.allocate(1, 1)};
  REQUIRE_FALSE(overflow.has_value());
  CHECK(overflow.error() == cn::container_error::full);
  CHECK(a.bytes_used() == arena_t::capacity());  // unchanged on failure
}

TEST_CASE("nexenne::container::scratch_pad rejects an allocation larger than the whole arena") {
  arena_t a;
  cn::scratch_pad scratch{a};
  auto const oversized{scratch.allocate(arena_t::capacity() + 1, 1)};
  REQUIRE_FALSE(oversized.has_value());
  CHECK(oversized.error() == cn::container_error::full);
  CHECK(a.bytes_used() == 0);

  auto const typed{scratch.allocate<int>(arena_t::capacity())};  // far too many ints
  REQUIRE_FALSE(typed.has_value());
  CHECK(typed.error() == cn::container_error::full);
  CHECK(a.bytes_used() == 0);
}

TEST_CASE("nexenne::container::scratch_pad rewind makes the full capacity reusable again") {
  arena_t a;
  {
    cn::scratch_pad scratch{a};
    REQUIRE(scratch.allocate(arena_t::capacity(), 1).has_value());
    CHECK(a.bytes_used() == arena_t::capacity());
  }
  CHECK(a.bytes_used() == 0);  // rewound on scope exit
  // The whole buffer is available once more.
  CHECK(a.allocate(arena_t::capacity(), 1).has_value());
}

TEST_CASE("nexenne::container::scratch_pad typed allocate defaults to a single object") {
  arena_t a;
  cn::scratch_pad scratch{a};
  auto const one{scratch.allocate<double>()};  // count defaults to 1
  REQUIRE(one.has_value());
  CHECK(a.bytes_used() == sizeof(double));
}

TEST_CASE("nexenne::container::scratch_pad zero-size allocation succeeds and consumes no bytes") {
  arena_t a;
  cn::scratch_pad scratch{a};
  auto const empty{scratch.allocate(0, 1)};
  REQUIRE(empty.has_value());
  CHECK(a.bytes_used() == 0);

  auto const none{scratch.allocate<int>(0)};
  REQUIRE(none.has_value());
  CHECK(a.bytes_used() == 0);
}

TEST_CASE("nexenne::container::scratch_pad emplace constructs a non-trivial object in place") {
  // The header's lifetime contract: emplace constructs the object, but the
  // scratch_pad's rewind does NOT run the destructor. The caller must
  // std::destroy_at a non-trivial object before the region is reclaimed.
  cn::linear_arena<256> a;
  std::string const long_value{"a string long enough to defeat the small-string buffer"};
  {
    cn::scratch_pad scratch{a};
    auto const text{scratch.emplace<std::string>(long_value)};
    REQUIRE(text.has_value());
    CHECK(**text == long_value);
    CHECK(a.bytes_used() >= sizeof(std::string));
    std::destroy_at(*text);  // caller-owned lifetime: release the heap buffer
  }
  CHECK(a.bytes_used() == 0);  // storage rewound on scope exit
}

TEST_CASE("nexenne::container::scratch_pad observers are const-correct") {
  arena_t a;
  REQUIRE(a.allocate(12, 1).has_value());
  cn::scratch_pad scratch{a};
  cn::scratch_pad<arena_t> const& const_ref{scratch};

  // saved_offset() and the const arena() overload are callable through a const
  // reference and report the checkpoint state.
  CHECK(const_ref.saved_offset() == 12);
  CHECK(const_ref.arena().bytes_used() == 12);
  static_assert(std::is_same_v<decltype(const_ref.arena()), arena_t const&>);
  static_assert(std::is_same_v<decltype(scratch.arena()), arena_t&>);

  // The non-const arena() returns a mutable reference usable for allocation.
  REQUIRE(scratch.arena().allocate(4, 1).has_value());
  CHECK(scratch.arena().bytes_used() == 16);
}

TEST_CASE("nexenne::container::scratch_pad nested guards restore offsets in LIFO order") {
  arena_t a;
  REQUIRE(a.allocate(8, 1).has_value());
  auto const base{a.bytes_used()};
  {
    cn::scratch_pad first{a};
    REQUIRE(first.allocate(8, 1).has_value());
    auto const after_first{a.bytes_used()};
    {
      cn::scratch_pad second{a};
      REQUIRE(second.allocate(8, 1).has_value());
      {
        cn::scratch_pad third{a};
        REQUIRE(third.allocate(8, 1).has_value());
        CHECK(third.saved_offset() == after_first + 8);
      }
      CHECK(a.bytes_used() == after_first + 8);  // third rewound
    }
    CHECK(a.bytes_used() == after_first);  // second rewound
  }
  CHECK(a.bytes_used() == base);  // first rewound; base allocation intact
}

}  // namespace
