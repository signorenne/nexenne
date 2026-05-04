/**
 * @file
 * @brief Tests for nexenne::utility::buffer_cursor.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <span>

#include <nexenne/utility/buffer_cursor.hpp>

namespace {

using nexenne::utility::buffer_cursor;

}  // namespace

// Construction, queries and navigation are all usable at compile time.
static_assert([] {
  auto storage{std::array<std::byte, 8>{}};
  auto cur{buffer_cursor{std::span{storage}}};
  if (cur.position() != 0 || cur.size() != 8 || cur.remaining() != 8) {
    return false;
  }
  cur.advance(3);
  if (cur.position() != 3 || cur.remaining() != 5 || !cur.has(5) || cur.has(6)) {
    return false;
  }
  auto const chunk{cur.take(2)};
  if (chunk.size() != 2 || cur.position() != 5) {
    return false;
  }
  cur.rewind();
  if (cur.position() != 0 || cur.exhausted()) {
    return false;
  }
  cur.put(std::byte{0x09});  // write one byte and advance
  cur.seek(0);
  return cur.next() == std::byte{0x09} && cur.position() == 1 && cur.consumed().size() == 1;
}());

TEST_CASE("nexenne::utility::buffer_cursor tracks position and remaining") {
  auto storage{std::array<std::byte, 4>{}};
  auto cur{buffer_cursor{std::span{storage}}};
  CHECK(cur.size() == 4);
  CHECK(cur.position() == 0);
  CHECK(cur.remaining() == 4);
  CHECK(cur.has(4));
  CHECK_FALSE(cur.has(5));
  CHECK_FALSE(cur.exhausted());

  cur.advance(4);
  CHECK(cur.position() == 4);
  CHECK(cur.remaining() == 0);
  CHECK(cur.exhausted());
  CHECK(cur.has(0));
}

TEST_CASE("nexenne::utility::buffer_cursor take advances, peek does not") {
  auto storage{std::array<std::byte, 6>{}};
  auto cur{buffer_cursor{std::span{storage}}};

  auto const viewed{cur.peek(3)};
  CHECK(viewed.size() == 3);
  CHECK(cur.position() == 0);  // peek leaves the cursor put

  auto const taken{cur.take(3)};
  CHECK(taken.size() == 3);
  CHECK(cur.position() == 3);
  CHECK(cur.data() == storage.data() + 3);
}

TEST_CASE(
  "nexenne::utility::buffer_cursor put writes a byte, next reads one, consumed reports the prefix"
) {
  auto storage{std::array<std::byte, 3>{}};
  auto wcur{buffer_cursor{std::span{storage}}};
  wcur.put(std::byte{0x11});
  wcur.put(std::byte{0x22});
  CHECK(wcur.position() == 2);
  CHECK(wcur.consumed().size() == 2);  // the two bytes written so far

  auto rcur{buffer_cursor{std::span<std::byte const>{storage}}};
  CHECK(std::to_integer<int>(rcur.next()) == 0x11);
  CHECK(std::to_integer<int>(rcur.next()) == 0x22);
  CHECK(rcur.position() == 2);
}

TEST_CASE("nexenne::utility::buffer_cursor seek moves to an absolute offset") {
  auto storage{std::array<std::byte, 8>{}};
  auto cur{buffer_cursor{std::span{storage}}};
  cur.seek(5);
  CHECK(cur.position() == 5);
  CHECK(cur.remaining() == 3);
  cur.retreat(2);
  CHECK(cur.position() == 3);
  cur.retreat();  // steps back one by default
  CHECK(cur.position() == 2);
  cur.seek(0);
  CHECK(cur.position() == 0);
}

TEST_CASE("nexenne::utility::buffer_cursor over an empty buffer is exhausted from the start") {
  auto cur{buffer_cursor{std::span<std::byte>{}}};
  CHECK(cur.size() == 0);
  CHECK(cur.position() == 0);
  CHECK(cur.remaining() == 0);
  CHECK(cur.exhausted());
  CHECK(cur.has(0));  // zero more elements always fit
  CHECK_FALSE(cur.has(1));
  CHECK(cur.data() == nullptr);  // one past the end of an empty span
  CHECK(cur.buffer().empty());
  CHECK(cur.consumed().empty());
  cur.seek(0);  // the only valid seek target
  cur.advance(0);
  cur.rewind();
  CHECK(cur.position() == 0);
}

TEST_CASE("nexenne::utility::buffer_cursor peek(0) and take(0) are empty and do not move") {
  auto storage{std::array<std::byte, 2>{std::byte{0x01}, std::byte{0x02}}};
  auto cur{buffer_cursor{std::span{storage}}};

  CHECK(cur.peek(0).empty());
  CHECK(cur.take(0).empty());
  CHECK(cur.position() == 0);  // a zero-length take does not advance

  cur.advance(2);  // exhausted: zero-length views are still valid
  CHECK(cur.peek(0).empty());
  CHECK(cur.take(0).empty());
  CHECK(cur.position() == 2);
}

TEST_CASE("nexenne::utility::buffer_cursor take returns the exact underlying bytes") {
  auto storage{std::array<std::byte, 5>{
    std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}, std::byte{0x42}
  }};
  auto cur{buffer_cursor{std::span{storage}}};
  cur.advance(1);

  auto const taken{cur.take(3)};  // must alias storage[1..3], not just have size 3
  REQUIRE(taken.size() == 3);
  CHECK(taken.data() == storage.data() + 1);
  CHECK(std::to_integer<int>(taken[0]) == 0xAD);
  CHECK(std::to_integer<int>(taken[1]) == 0xBE);
  CHECK(std::to_integer<int>(taken[2]) == 0xEF);

  // Writing through the taken span writes the underlying storage.
  taken[0] = std::byte{0x77};
  CHECK(std::to_integer<int>(storage[1]) == 0x77);

  // peek at the new position views the same byte take would return.
  auto const peeked{cur.peek(1)};
  CHECK(peeked.data() == storage.data() + 4);
  CHECK(std::to_integer<int>(peeked[0]) == 0x42);
}

TEST_CASE("nexenne::utility::buffer_cursor over a const span is a read cursor") {
  auto const storage{std::array<std::byte, 2>{std::byte{0xAB}, std::byte{0xCD}}};
  auto cur{buffer_cursor{std::span{storage}}};
  static_assert(std::is_const_v<typename decltype(cur)::value_type>);
  CHECK(std::to_integer<int>(*cur.data()) == 0xAB);
  CHECK(cur.buffer().size() == 2);
}
