/**
 * @file
 * @brief Tests for nexenne::container::linear_arena.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <nexenne/container/linear_arena.hpp>

namespace {

namespace cn = nexenne::container;
using arena = cn::linear_arena<256>;

static_assert(arena::capacity() == 256);
static_assert(arena::buffer_size == 256);
static_assert(arena::max_size() == 256);

// The constructor, observers, reset and rewind_to are constexpr. Allocation is
// not (it hands out raw storage), so a constexpr arena can only exercise the
// bookkeeping surface. Confirm the whole non-allocating API folds at compile
// time.
static_assert([] {
  arena a;
  if (!a.empty() || a.bytes_used() != 0 || a.bytes_available() != 256) {
    return false;
  }
  if (a.high_water_mark() != 0) {
    return false;
  }
  a.reset();
  a.rewind_to(0);
  a.clear_high_water_mark();
  return a.empty() && a.high_water_mark() == 0;
}());

[[nodiscard]] auto address_of(void const* p) noexcept -> std::uintptr_t {
  return reinterpret_cast<std::uintptr_t>(p);
}

struct alignas(64) over_aligned {
  std::byte payload[64];
};

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

TEST_CASE("nexenne::container::linear_arena returns pointers aligned to the request") {
  arena a;
  // alignof(max_align_t) is the largest alignment the buffer can guarantee;
  // it is at least 16 on the targeted platforms, so 1..16 are always legal.
  for (cn::linear_arena<256>::size_type align{1}; align <= 16; align *= 2) {
    // A one-byte gap before each request forces non-trivial padding so the
    // alignment math actually has to do work.
    REQUIRE(a.allocate(1, 1).has_value());
    auto const block{a.allocate(8, align)};
    REQUIRE(block.has_value());
    CHECK(address_of(*block) % align == 0);
  }
}

TEST_CASE("nexenne::container::linear_arena counts alignment padding in bytes_used") {
  arena a;
  // First byte lands at offset 0, leaving the bump pointer at 1.
  REQUIRE(a.allocate(1, 1).has_value());
  CHECK(a.bytes_used() == 1);

  // A 16-aligned request must skip offsets 1..15 (15 padding bytes) before
  // placing 8 bytes, so the new offset is 16 + 8 == 24.
  auto const block{a.allocate(8, 16)};
  REQUIRE(block.has_value());
  CHECK(address_of(*block) % 16 == 0);
  CHECK(a.bytes_used() == 24);
}

TEST_CASE("nexenne::container::linear_arena aligns allocations up to max_align_t") {
  cn::linear_arena<512> a;
  // The arena guarantees alignment up to alignof(std::max_align_t); allocating a
  // more-aligned type is a static-assert precondition violation, so the maximum
  // supported alignment is tested here.
  // Skew the bump pointer so the typed allocation has to pad up.
  REQUIRE(a.allocate(3, 1).has_value());
  auto const block{a.allocate<std::max_align_t>()};
  REQUIRE(block.has_value());
  CHECK(address_of(*block) % alignof(std::max_align_t) == 0);

  auto const object{a.emplace<std::max_align_t>()};
  REQUIRE(object.has_value());
  CHECK(address_of(*object) % alignof(std::max_align_t) == 0);
  CHECK(*object != *block);
}

TEST_CASE("nexenne::container::linear_arena fills to capacity then reports full") {
  cn::linear_arena<32> a;
  auto const block{a.allocate(32, 1)};
  REQUIRE(block.has_value());
  CHECK(a.bytes_used() == 32);
  CHECK(a.bytes_available() == 0);

  // One byte past an exactly-full arena is the boundary failure.
  CHECK(a.allocate(1, 1).error() == cn::container_error::full);
  // The failed request must leave the arena untouched.
  CHECK(a.bytes_used() == 32);
}

TEST_CASE("nexenne::container::linear_arena rejects an allocation larger than capacity") {
  cn::linear_arena<32> a;
  auto const block{a.allocate(33, 1)};
  REQUIRE_FALSE(block.has_value());
  CHECK(block.error() == cn::container_error::full);
  // An over-capacity request never partially consumes the buffer.
  CHECK(a.empty());
  CHECK(a.bytes_available() == 32);
}

TEST_CASE("nexenne::container::linear_arena fails when padding pushes a fitting size over") {
  cn::linear_arena<32> a;
  // Consume 20 bytes, leaving 12 raw bytes free.
  REQUIRE(a.allocate(20, 1).has_value());
  CHECK(a.bytes_available() == 12);

  // 12 bytes would fit raw, but a 16-byte alignment pads the offset from 20 up
  // to 32, leaving nothing for the 12 requested bytes.
  auto const padded{a.allocate(12, 16)};
  REQUIRE_FALSE(padded.has_value());
  CHECK(padded.error() == cn::container_error::full);
  // The arena is unchanged after the padding-induced failure.
  CHECK(a.bytes_used() == 20);

  // The same 12 bytes at alignment 1 still fits exactly.
  CHECK(a.allocate(12, 1).has_value());
  CHECK(a.bytes_available() == 0);
}

TEST_CASE("nexenne::container::linear_arena reset makes the whole capacity allocatable again") {
  arena a;
  REQUIRE(a.allocate(256, 1).has_value());
  CHECK(a.bytes_available() == 0);
  CHECK(a.allocate(1, 1).error() == cn::container_error::full);

  a.reset();
  CHECK(a.empty());
  CHECK(a.bytes_used() == 0);
  CHECK(a.bytes_available() == 256);

  // The full capacity is handed out again after a reset, confirming the prior
  // allocation's space returned.
  auto const reused{a.allocate(256, 1)};
  REQUIRE(reused.has_value());
  CHECK(a.bytes_available() == 0);
}

TEST_CASE("nexenne::container::linear_arena rewind_to reuses the rewound space") {
  arena a;
  REQUIRE(a.allocate(20, 1).has_value());
  auto const checkpoint{a.bytes_used()};
  auto const second{a.allocate(50, 1)};
  REQUIRE(second.has_value());
  CHECK(a.bytes_used() == 70);

  a.rewind_to(checkpoint);
  CHECK(a.bytes_used() == checkpoint);

  // Space above the checkpoint is handed back out; the new block reuses the
  // exact address vacated by the rewound allocation.
  auto const reused{a.allocate(50, 1)};
  REQUIRE(reused.has_value());
  CHECK(a.bytes_used() == 70);
  CHECK(address_of(*reused) == address_of(*second));
}

TEST_CASE("nexenne::container::linear_arena rewind_to leaves the high-water mark intact") {
  arena a;
  REQUIRE(a.allocate(100, 1).has_value());
  REQUIRE(a.allocate(50, 1).has_value());  // peak 150
  CHECK(a.high_water_mark() == 150);

  a.rewind_to(40);
  CHECK(a.bytes_used() == 40);
  // The peak is a workload-sizing aid, so a rewind must not lower it.
  CHECK(a.high_water_mark() == 150);
}

TEST_CASE("nexenne::container::linear_arena allows zero-size allocations") {
  arena a;
  // A zero-byte request succeeds and consumes no bytes when no padding is due.
  auto const empty_block{a.allocate(0, 1)};
  REQUIRE(empty_block.has_value());
  CHECK(a.bytes_used() == 0);

  // A zero-byte request at a coarse alignment still advances by the padding it
  // forces, even though it reserves no payload.
  REQUIRE(a.allocate(1, 1).has_value());
  auto const padded_empty{a.allocate(0, 16)};
  REQUIRE(padded_empty.has_value());
  CHECK(address_of(*padded_empty) % 16 == 0);
  CHECK(a.bytes_used() == 16);

  // A zero-size typed allocation is also well-formed.
  CHECK(a.allocate<int>(0).has_value());
}

TEST_CASE("nexenne::container::linear_arena typed allocate sizes for count objects") {
  arena a;
  auto const block{a.allocate<std::int32_t>(3)};
  REQUIRE(block.has_value());
  CHECK(address_of(*block) % alignof(std::int32_t) == 0);
  // Three int32_t spans 12 bytes from a fresh arena.
  CHECK(a.bytes_used() == 3 * sizeof(std::int32_t));

  // The reserved storage is writable across the whole span.
  for (int i{0}; i < 3; ++i) {
    (*block)[i] = i + 1;
  }
  CHECK((*block)[0] == 1);
  CHECK((*block)[2] == 3);
}

TEST_CASE("nexenne::container::linear_arena emplace constructs a non-trivial object") {
  cn::linear_arena<256> a;
  // std::string is non-trivially-destructible; per the header contract the
  // arena does not run destructors, so the caller must std::destroy_at it.
  auto const text{a.emplace<std::string>("nexenne linear arena")};
  REQUIRE(text.has_value());
  CHECK(**text == "nexenne linear arena");
  CHECK(address_of(*text) % alignof(std::string) == 0);

  // The arena will not destroy it on reset; clean up the live object first to
  // avoid leaking its heap buffer.
  std::destroy_at(*text);
  a.reset();
  CHECK(a.empty());
}

TEST_CASE("nexenne::container::linear_arena emplace forwards constructor arguments") {
  arena a;
  auto const filled{a.emplace<std::string>(std::size_t{5}, 'x')};
  REQUIRE(filled.has_value());
  CHECK(**filled == "xxxxx");
  std::destroy_at(*filled);
}

TEST_CASE("nexenne::container::linear_arena propagates full through typed and emplace paths") {
  cn::linear_arena<4> tiny;
  // sizeof(double) (8) exceeds the 4-byte buffer, so the typed path forwards
  // the underlying full error rather than crashing.
  auto const typed{tiny.allocate<double>()};
  REQUIRE_FALSE(typed.has_value());
  CHECK(typed.error() == cn::container_error::full);

  auto const built{tiny.emplace<double>(1.0)};
  REQUIRE_FALSE(built.has_value());
  CHECK(built.error() == cn::container_error::full);
  // No partial construction took place on the failure path.
  CHECK(tiny.empty());
}

TEST_CASE("nexenne::container::linear_arena exposes const observers") {
  arena a;
  REQUIRE(a.allocate(40, 8).has_value());
  arena const& ca{a};
  // Every observer must be callable through a const reference.
  CHECK(ca.bytes_used() == 40);
  CHECK(ca.bytes_available() == 216);
  CHECK_FALSE(ca.empty());
  CHECK(ca.high_water_mark() == 40);
  CHECK(ca.capacity() == 256);
  CHECK(ca.max_size() == 256);
}

}  // namespace
