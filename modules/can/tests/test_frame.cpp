/**
 * @file
 * @brief Tests for the CAN wire frame.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <format>
#include <type_traits>

#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto byte_of(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

TEST_CASE("frame: trivially copyable so it can be memcpy'd to and from the kernel struct") {
  static_assert(std::is_trivially_copyable_v<nc::frame>);
}

TEST_CASE("frame: default is an empty Classic CAN frame") {
  nc::frame const f;
  CHECK(f.length() == 0);
  CHECK_FALSE(f.is_fd());
  CHECK(f.id() == nc::can_id{});
  CHECK(f.data().empty());
}

TEST_CASE("frame: classic factory copies the payload and reports its length") {
  std::array const payload{byte_of(0xDE), byte_of(0xAD), byte_of(0xBE), byte_of(0xEF)};
  auto const made{nc::frame::classic(nc::can_id::standard(0x123), payload)};
  REQUIRE(made.has_value());
  auto const& f{*made};
  CHECK_FALSE(f.is_fd());
  CHECK(f.length() == 4);
  REQUIRE(f.data().size() == 4);
  CHECK(f.data()[0] == byte_of(0xDE));
  CHECK(f.data()[3] == byte_of(0xEF));
}

TEST_CASE("frame: classic rejects a payload larger than 8 bytes") {
  std::array<std::byte, 9> const big{};
  auto const made{nc::frame::classic(nc::can_id::standard(0x1), big)};
  REQUIRE_FALSE(made.has_value());
  CHECK(made.error() == nc::can_error::payload_too_large);
}

TEST_CASE("frame: fd factory sets fdf and carries the requested flags") {
  std::array<std::byte, 16> payload{};
  payload[0] = byte_of(0x55);
  auto const made{nc::frame::fd(nc::can_id::extended(0x18FEF100), payload, nc::fd_flag::brs)};
  REQUIRE(made.has_value());
  auto const& f{*made};
  CHECK(f.is_fd());
  CHECK(f.flags().has(nc::fd_flag::fdf));
  CHECK(f.flags().has(nc::fd_flag::brs));
  CHECK_FALSE(f.flags().has(nc::fd_flag::esi));
  CHECK(f.length() == 16);
  CHECK(f.data()[0] == byte_of(0x55));
}

TEST_CASE("frame: fd rejects a payload larger than 64 bytes") {
  std::array<std::byte, 65> const big{};
  auto const made{nc::frame::fd(nc::can_id::extended(0x1), big)};
  REQUIRE_FALSE(made.has_value());
  CHECK(made.error() == nc::can_error::payload_too_large);
}

TEST_CASE("frame: a mutable data span writes back into the payload") {
  std::array const payload{byte_of(0), byte_of(0)};
  auto made{nc::frame::classic(nc::can_id::standard(0x10), payload)};
  REQUIRE(made.has_value());
  made->data()[1] = byte_of(0x7F);
  CHECK(made->data()[1] == byte_of(0x7F));
}

TEST_CASE("frame: equality ignores the timestamp but compares the bytes") {
  std::array const payload{byte_of(0x01), byte_of(0x02)};
  auto a{nc::frame::classic(nc::can_id::standard(0x1), payload)};
  auto b{nc::frame::classic(nc::can_id::standard(0x1), payload)};
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());

  b->timestamp_ns() = 123456;
  CHECK(*a == *b);  // timestamp is not part of equality

  b->data()[0] = byte_of(0xFF);
  CHECK(*a != *b);
}

TEST_CASE("frame: format prints the id, length, flags, and bytes") {
  std::array const payload{byte_of(0xDE), byte_of(0xAD)};
  auto const f{nc::frame::classic(nc::can_id::standard(0x123), payload)};
  REQUIRE(f.has_value());
  CHECK(std::format("{}", *f) == "frame(0x123 std, len=2, [DE AD])");

  std::array const fd_payload{byte_of(0x01)};
  auto const g{nc::frame::fd(nc::can_id::extended(0x1), fd_payload, nc::fd_flag::brs)};
  REQUIRE(g.has_value());
  CHECK(std::format("{}", *g) == "frame(0x00000001 ext fd brs, len=1, [01])");
}

TEST_CASE("frame: filled creates a classic frame with every byte set to the fill value") {
  auto const f{nc::frame::filled(nc::can_id::standard(0x10), 8, byte_of(0xFF))};
  REQUIRE(f.has_value());
  CHECK_FALSE(f->is_fd());
  CHECK(f->length() == 8);
  for (std::byte const value : f->data()) {
    CHECK(value == byte_of(0xFF));
  }
  CHECK(
    nc::frame::filled(nc::can_id::standard(0x1), 9, byte_of(0)).error()
    == nc::can_error::payload_too_large
  );
}

TEST_CASE("frame: fd_filled creates an FD frame filled with the value") {
  auto const f{nc::frame::fd_filled(nc::can_id::extended(0x1), 16, byte_of(0xFF), nc::fd_flag::brs)
  };
  REQUIRE(f.has_value());
  CHECK(f->is_fd());
  CHECK(f->flags().has(nc::fd_flag::brs));
  CHECK(f->length() == 16);
  CHECK(f->data()[15] == byte_of(0xFF));
  CHECK(
    nc::frame::fd_filled(nc::can_id::extended(0x1), 65, byte_of(0)).error()
    == nc::can_error::payload_too_large
  );
}

TEST_CASE("frame factories accept an empty payload (RTR-style) without UB") {
  // Regression: an empty span has data() == nullptr, and the old memcpy path
  // passed a null pointer to memcpy, which is UB even for a zero count.
  auto const classic{nc::frame::classic(nc::can_id::standard(0x123), {})};
  REQUIRE(classic.has_value());
  CHECK(classic->length() == 0);
  CHECK(classic->data().empty());
  CHECK_FALSE(classic->is_fd());

  auto const fd{nc::frame::fd(nc::can_id::extended(0x1), {})};
  REQUIRE(fd.has_value());
  CHECK(fd->length() == 0);
  CHECK(fd->data().empty());
  CHECK(fd->is_fd());
}

TEST_CASE("frame factories are usable at compile time") {
  // Regression: the factories were neither constexpr nor noexcept.
  static_assert(noexcept(nc::frame::classic(nc::can_id::standard(1), {})));
  constexpr auto payload{std::array<std::byte, 2>{byte_of(0xDE), byte_of(0xAD)}};
  constexpr auto f{nc::frame::classic(nc::can_id::standard(0x123), payload)};
  static_assert(f.has_value());
  static_assert(f->length() == 2);
  static_assert(f->data()[0] == byte_of(0xDE));
  constexpr auto filled{nc::frame::filled(nc::can_id::standard(0x1), 3, byte_of(0xFF))};
  static_assert(filled.has_value() && filled->data()[2] == byte_of(0xFF));
}

TEST_CASE("fd_flag and its flag set are formattable") {
  CHECK(nc::to_string(nc::fd_flag::brs) == "brs");
  CHECK(std::format("{}", nc::fd_flag::esi) == "esi");

  auto const f{nc::frame::fd(nc::can_id::standard(0x1), {}, nc::fd_flag::brs)};
  REQUIRE(f.has_value());
  CHECK(nc::to_string(f->flags()) == "fdf brs");
  CHECK(std::format("{}", f->flags()) == "fdf brs");

  CHECK(nc::to_string(nexenne::utility::flags<nc::fd_flag>{}).empty());
}

TEST_CASE("frame equality holds after growing the length through the accessor") {
  auto a{nc::frame::filled(nc::can_id::standard(0x1), 2, byte_of(0xAA)).value()};
  auto b{a};
  CHECK(a == b);
  a.length() = 4;  // mutable accessor on an lvalue
  a.data()[2] = byte_of(0xAA);
  a.data()[3] = byte_of(0xAA);
  CHECK_FALSE(a == b);
  b.length() = 4;
  b.data()[2] = byte_of(0xAA);
  b.data()[3] = byte_of(0xAA);
  CHECK(a == b);
}

}  // namespace
