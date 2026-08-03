/**
 * @file
 * @brief filters: setting identifier masks correctly.
 *
 * A filter is an identifier plus a mask: an incoming id passes when
 * (id & mask) == (filter_id & mask). A mask bit of 1 means "must match here", a
 * mask bit of 0 means "don't care". This example shows the three filter factories
 * and how a mask accepts a whole family of ids, both as a standalone test and as
 * the receive filter on a loopback bus.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/filter.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/loopback_bus.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto byte_of(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

}  // namespace

auto main() -> int {
  // Exact match: accept only standard id 0x100 (and its format), nothing else.
  auto const exact{nc::filter::equals(nc::can_id::standard(0x100))};
  std::println("{}", exact);
  std::println(
    "  0x100 -> {}, 0x101 -> {}",
    exact.matches(nc::can_id::standard(0x100)),
    exact.matches(nc::can_id::standard(0x101))
  );

  // Family match with a mask: id 0x700, mask 0x700 means "top three bits must be
  // 0x7, the low byte is don't-care", so it accepts 0x700..0x7FF.
  auto const family{nc::filter::standard(0x700, 0x700)};
  std::println("{}", family);
  std::println(
    "  0x700 -> {}, 0x7AB -> {}, 0x100 -> {}",
    family.matches(nc::can_id::standard(0x700)),
    family.matches(nc::can_id::standard(0x7AB)),
    family.matches(nc::can_id::standard(0x100))
  );

  // Extended (29-bit) filter: matches an extended id, not a standard one of the
  // same value, because the extended-frame flag is part of the mask.
  auto const j1939{nc::filter::extended(0x18FEF100)};
  std::println("{}", j1939);
  std::println(
    "  ext 0x18FEF100 -> {}, std 0x100 -> {}",
    j1939.matches(nc::can_id::extended(0x18FEF100)),
    j1939.matches(nc::can_id::standard(0x100))
  );

  // The same filter type drives a bus: frames that fail every filter are dropped
  // on receive (the SocketCAN backend pushes these into the kernel as hardware
  // filters; the loopback bus filters in software).
  nc::loopback_bus bus;
  std::array const filters{nc::filter::standard(0x700, 0x700)};
  nexenne::utility::discard(bus.set_filters(filters));

  std::array const payload{byte_of(0x01)};
  nexenne::utility::discard(
    bus.send(*nc::frame::classic(nc::can_id::standard(0x123), payload))
  );  // dropped
  nexenne::utility::discard(
    bus.send(*nc::frame::classic(nc::can_id::standard(0x7AB), payload))
  );  // kept
  if (auto const got{bus.receive()}; got && got->has_value()) {
    std::println("received past the filter: {}", **got);
  }

  return 0;
}
