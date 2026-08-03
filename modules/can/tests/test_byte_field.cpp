/**
 * @file
 * @brief Tests for byte-array and text fields.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include <nexenne/can/byte_field.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto b(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

auto frame_of(std::span<std::byte const> const payload) -> nc::frame {
  return *nc::frame::classic(nc::can_id::standard(0x100), payload);
}

TEST_CASE("byte_field: accessors and required_length") {
  nc::byte_field const field{2, 4, "blob"};
  CHECK(field.start_byte() == 2);
  CHECK(field.length() == 4);
  CHECK(field.name() == "blob");
  CHECK(field.required_length() == 6);

  nc::byte_field const dflt;
  CHECK(dflt.start_byte() == 0);
  CHECK(dflt.length() == 1);
}

TEST_CASE("byte_field: read_bytes returns a view of the region") {
  std::array const payload{b(0x11), b(0x22), b(0x33), b(0x44)};
  auto const f{frame_of(payload)};
  nc::byte_field const field{1, 2};
  auto const view{nc::read_bytes(field, f)};
  REQUIRE(view.has_value());
  REQUIRE(view->size() == 2);
  CHECK((*view)[0] == b(0x22));
  CHECK((*view)[1] == b(0x33));
}

TEST_CASE("byte_field: a field past the frame length is out of range") {
  std::array const payload{b(0x11), b(0x22)};
  auto const f{frame_of(payload)};
  nc::byte_field const field{1, 4};  // needs 5 bytes, frame has 2
  CHECK(nc::read_bytes(field, f).error() == nc::can_error::signal_out_of_range);
}

TEST_CASE("byte_field: write_bytes copies and leaves the tail untouched when source is short") {
  std::array const init{b(0xFF), b(0xFF), b(0xFF), b(0xFF)};
  auto f{frame_of(init)};
  nc::byte_field const field{0, 4};
  std::array const src{b(0xAA), b(0xBB)};
  REQUIRE(nc::write_bytes(field, f, src).has_value());
  CHECK(f.data()[0] == b(0xAA));
  CHECK(f.data()[1] == b(0xBB));
  CHECK(f.data()[2] == b(0xFF));  // untouched
  CHECK(f.data()[3] == b(0xFF));
}

TEST_CASE("byte_field: text round-trips and pads with NUL") {
  std::array<std::byte, 6> zeros{};
  auto f{frame_of(zeros)};
  nc::byte_field const field{0, 6, "tag"};

  REQUIRE(nc::write_text(field, f, "ABC").has_value());
  CHECK(f.data()[3] == b(0));  // NUL padded
  auto const text{nc::read_text(field, f)};
  REQUIRE(text.has_value());
  CHECK(*text == "ABC");  // trailing NULs trimmed
}

TEST_CASE("byte_field: write_text clears stale bytes when the new text is shorter") {
  std::array const init{b('X'), b('X'), b('X'), b('X')};
  auto f{frame_of(init)};
  nc::byte_field const field{0, 4};
  REQUIRE(nc::write_text(field, f, "Z").has_value());
  auto const text{nc::read_text(field, f)};
  REQUIRE(text.has_value());
  CHECK(*text == "Z");
}

TEST_CASE("byte_field: a field near the 16-bit limit does not wrap past the bounds check") {
  // Regression: required_length() was computed in uint16_t, so start 65535 + len 2
  // wrapped to 1 and slipped past the guard, handing out an out-of-bounds span.
  nc::byte_field const field{65535, 2, "overflow"};
  CHECK(field.required_length() == 65537U);  // wider type, no wrap

  std::array const payload{b(0x11), b(0x22), b(0x33), b(0x44)};
  auto f{frame_of(payload)};
  CHECK(nc::read_bytes(field, f).error() == nc::can_error::signal_out_of_range);
  CHECK(nc::write_bytes(field, f, payload).error() == nc::can_error::signal_out_of_range);
  CHECK(nc::read_text(field, f).error() == nc::can_error::signal_out_of_range);
}

}  // namespace
