/**
 * @file
 * @brief filters: setting identifier masks correctly.
 *
 * A filter is an identifier plus a mask: an incoming id passes when
 * (id & mask) == (filter_id & mask). A mask bit of 1 means "must match here", a
 * mask bit of 0 means "don't care". This example shows the three filter factories
 * and how a mask accepts a whole family of ids, both as a standalone test and as
 * the receive filter on a loopback bus.
 *
 * The program walks four steps:
 *
 * 1. Exact match: \c equals accepts standard id 0x100 in its format and nothing
 *    else.
 * 2. Family match: id 0x700 with mask 0x700 requires the top three id bits to be
 *    0x7 and ignores the low byte, so it accepts 0x700 to 0x7FF.
 * 3. Extended match: the extended-frame flag is part of the mask, so an extended
 *    filter rejects a standard id of the same value.
 * 4. On a bus, frames that fail every filter are dropped on receive; the
 *    SocketCAN backend installs the filters in the kernel, the loopback bus
 *    applies them in software. Of 0x123 and 0x7AB only 0x7AB gets through.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/filter.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/loopback_bus.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto byte_of(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

}  // namespace

auto main() -> int {
  auto const exact{nc::filter::equals(nc::can_id::standard(0x100))};
  std::println("{}", exact);
  std::println(
    "  0x100 -> {}, 0x101 -> {}",
    exact.matches(nc::can_id::standard(0x100)),
    exact.matches(nc::can_id::standard(0x101))
  );

  auto const family{nc::filter::standard(0x700, 0x700)};
  std::println("{}", family);
  std::println(
    "  0x700 -> {}, 0x7AB -> {}, 0x100 -> {}",
    family.matches(nc::can_id::standard(0x700)),
    family.matches(nc::can_id::standard(0x7AB)),
    family.matches(nc::can_id::standard(0x100))
  );

  auto const j1939{nc::filter::extended(0x18FEF100)};
  std::println("{}", j1939);
  std::println(
    "  ext 0x18FEF100 -> {}, std 0x100 -> {}",
    j1939.matches(nc::can_id::extended(0x18FEF100)),
    j1939.matches(nc::can_id::standard(0x100))
  );

  nc::loopback_bus bus;
  std::array const filters{nc::filter::standard(0x700, 0x700)};
  nexenne::utility::ignore(bus.apply_filters(filters));

  std::array const payload{byte_of(0x01)};
  nexenne::utility::ignore(bus.send(*nc::frame::classic(nc::can_id::standard(0x123), payload)));
  nexenne::utility::ignore(bus.send(*nc::frame::classic(nc::can_id::standard(0x7AB), payload)));
  if (auto const got{bus.receive()}; got && got->has_value()) {
    std::println("received past the filter: {}", **got);
  }

  return 0;
}
