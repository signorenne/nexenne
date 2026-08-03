/**
 * @file
 * @brief Tests for the Classic CAN and CAN FD data length conversions.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include <nexenne/can/dlc.hpp>

namespace {

namespace nc = nexenne::can;

TEST_CASE("classic DLC: 0 through 8 are identity, 9 through 15 clamp to 8") {
  for (std::uint8_t dlc{0}; dlc <= 8; ++dlc) {
    CHECK(nc::classic_dlc_to_length(dlc) == dlc);
  }
  for (std::uint8_t dlc{9}; dlc <= 15; ++dlc) {
    CHECK(nc::classic_dlc_to_length(dlc) == 8);
  }
  CHECK(nc::classic_length_to_dlc(5) == 5);
  CHECK(nc::classic_length_to_dlc(8) == 8);
  CHECK(nc::classic_length_to_dlc(20) == 8);
}

TEST_CASE("FD DLC: the full sixteen-entry table maps both directions") {
  constexpr std::array<std::uint8_t, 16> lengths{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
  };
  for (std::uint8_t dlc{0}; dlc < lengths.size(); ++dlc) {
    CHECK(nc::fd_dlc_to_length(dlc) == lengths[dlc]);
    // A length that is exactly a table entry maps back to the same DLC.
    CHECK(nc::fd_length_to_dlc(lengths[dlc]) == dlc);
  }
}

TEST_CASE("FD DLC: a non-discrete length rounds up to the next size") {
  CHECK(nc::fd_length_to_dlc(9) == 9);  // 9 -> 12 bytes -> DLC 9
  CHECK(nc::fd_padded_length(9) == 12);
  CHECK(nc::fd_padded_length(13) == 16);
  CHECK(nc::fd_padded_length(33) == 48);
  CHECK(nc::fd_padded_length(64) == 64);
  CHECK(nc::fd_padded_length(8) == 8);
}

TEST_CASE("length validity predicates") {
  static_assert(nc::is_valid_classic_length(8));
  static_assert(!nc::is_valid_classic_length(9));

  static_assert(nc::is_valid_fd_length(0));
  static_assert(nc::is_valid_fd_length(8));
  static_assert(nc::is_valid_fd_length(12));
  static_assert(nc::is_valid_fd_length(64));
  static_assert(!nc::is_valid_fd_length(9));
  static_assert(!nc::is_valid_fd_length(13));
}

TEST_CASE("FD DLC: a length above 64 saturates at DLC 15") {
  CHECK(nc::fd_length_to_dlc(65) == 15);
  CHECK(nc::fd_length_to_dlc(255) == 15);
  CHECK(nc::fd_padded_length(255) == 64);
  CHECK_FALSE(nc::is_valid_fd_length(65));
}

TEST_CASE("fd_length_to_dlc via the precomputed reverse table matches the forward table") {
  // Regression: fd_length_to_dlc now indexes a precomputed reverse table rather
  // than scanning; every length must still round-trip to the smallest covering DLC.
  for (std::uint16_t length{0}; length <= 64U; ++length) {
    auto const len{static_cast<std::uint8_t>(length)};
    auto const dlc{nc::fd_length_to_dlc(len)};
    CHECK(nc::fd_dlc_to_length(dlc) >= len);
    CHECK((dlc == 0U || nc::fd_dlc_to_length(static_cast<std::uint8_t>(dlc - 1U)) < len));
  }
  CHECK(nc::fd_dlc_to_length(15) == 64);
  static_assert(nc::fd_length_to_dlc(9) == 9);  // 9 bytes -> DLC 9 -> length 12
  static_assert(nc::fd_length_to_dlc(64) == 15);
}

}  // namespace
