/**
 * @file
 * @brief Tests for decoding CAN error frames into a controller state.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include <nexenne/can/bus.hpp>
#include <nexenne/can/error_frame.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto b(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

// Builds an error frame: an identifier with the error flag and the given class
// bits, plus an 8-byte payload.
auto error_frame_of(std::uint32_t const classes, std::array<std::byte, 8> const& data)
  -> nc::frame {
  auto const id{nc::can_id::from_raw(nc::error_flag | classes)};
  return *nc::frame::classic(id, data);
}

TEST_CASE("is_error_frame distinguishes error frames from data frames") {
  auto const data{*nc::frame::classic(nc::can_id::standard(0x100), std::array{b(0)})};
  CHECK_FALSE(nc::is_error_frame(data));
  CHECK(nc::is_error_frame(error_frame_of(nc::err_class_bus_off, {})));
}

TEST_CASE("decode_error_frame: a non-error frame decodes to nothing") {
  auto const data{*nc::frame::classic(nc::can_id::standard(0x100), std::array{b(0)})};
  CHECK_FALSE(nc::decode_error_frame(data).has_value());
}

TEST_CASE("decode_error_frame: the bus-off class yields the bus-off state") {
  auto const report{nc::decode_error_frame(error_frame_of(nc::err_class_bus_off, {}))};
  REQUIRE(report.has_value());
  CHECK(report->state == nc::bus_state::bus_off);
}

TEST_CASE("decode_error_frame: a passive controller status yields error-passive") {
  std::array<std::byte, 8> data{};
  data[1] = b(nc::err_controller_tx_passive);
  auto const report{nc::decode_error_frame(error_frame_of(nc::err_class_controller, data))};
  REQUIRE(report.has_value());
  CHECK(report->state == nc::bus_state::error_passive);
}

TEST_CASE("decode_error_frame: error counters are read from data bytes 6 and 7") {
  std::array<std::byte, 8> data{};
  data[6] = b(255);  // transmit error counter
  data[7] = b(130);  // receive error counter
  auto const report{nc::decode_error_frame(error_frame_of(nc::err_class_counters, data))};
  REQUIRE(report.has_value());
  CHECK(report->counters == nc::error_counters{255, 130});
  CHECK(report->state == nc::bus_state::error_active);
}

TEST_CASE("decode_error_frame: status and counter bytes are ignored without their class bits") {
  // A frame that is only arbitration-lost (no controller or counter class) but
  // happens to carry a passive status in byte 1 and counters in bytes 6/7 must
  // not be read as error-passive or as valid counters.
  std::array<std::byte, 8> data{};
  data[1] = b(nc::err_controller_tx_passive);
  data[6] = b(0x11);
  data[7] = b(0x22);
  // Class 0x02 is arbitration-lost (CAN_ERR_LOSTARB): neither controller nor counters.
  auto const report{nc::decode_error_frame(error_frame_of(std::uint32_t{0x02}, data))};
  REQUIRE(report.has_value());
  CHECK(report->state == nc::bus_state::error_active);   // not error_passive
  CHECK(report->counters == nc::error_counters{0, 0});   // counters class bit is clear
}

TEST_CASE("decode_error_frame is usable in a constant expression") {
  // Exercises the constexpr marking (frames are constexpr-constructible now).
  constexpr auto decoded{[] {
    auto const f{nc::frame::classic(nc::can_id::from_raw(nc::error_flag | nc::err_class_bus_off), {})};
    return f.has_value() && nc::decode_error_frame(*f).has_value();
  }()};
  static_assert(decoded, "decode_error_frame must work at compile time");
  CHECK(decoded);
}

}  // namespace
