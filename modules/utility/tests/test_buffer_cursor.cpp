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

TEST_CASE("nexenne::utility::buffer_cursor over a const span is a read cursor") {
  auto const storage{std::array<std::byte, 2>{std::byte{0xAB}, std::byte{0xCD}}};
  auto cur{buffer_cursor{std::span{storage}}};
  static_assert(std::is_const_v<typename decltype(cur)::value_type>);
  CHECK(std::to_integer<int>(*cur.data()) == 0xAB);
  CHECK(cur.buffer().size() == 2);
}
