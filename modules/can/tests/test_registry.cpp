/**
 * @file
 * @brief Tests for the receive-side registry: lookup, filters, and decoding.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <nexenne/can/codec.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/registry.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto b(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

auto build_db() -> nc::database {
  auto const status{nc::message_builder{nc::can_id::standard(0x100), "status"}
                      .add(
                        nc::signal_builder{}
                          .name("speed")
                          .start_bit(0)
                          .length(16)
                          .endianness(nc::byte_order::little_endian)
                          .scale(0.01)
                          .build()
                      )
                      .add(
                        nc::signal_builder{}
                          .name("oil_temp")
                          .start_bit(16)
                          .length(8)
                          .endianness(nc::byte_order::little_endian)
                          .scale(1.0)
                          .offset(-40.0)
                          .build()
                      )
                      .build()};
  auto const engine{nc::message_builder{nc::can_id::extended(0x18FEF100), "engine"}
                      .add(
                        nc::signal_builder{}
                          .name("rpm")
                          .start_bit(0)
                          .length(8)
                          .endianness(nc::byte_order::little_endian)
                          .build()
                      )
                      .build()};
  return nc::database_builder{}.add_message(status).add_message(engine).build();
}

TEST_CASE("registry: indexed find resolves identifiers in the database") {
  auto const db{build_db()};
  nc::registry const reg{db};

  auto const* const status{reg.find(nc::can_id::standard(0x100))};
  REQUIRE(status != nullptr);
  CHECK(status->name() == "status");
  CHECK(reg.find(nc::can_id::extended(0x18FEF100))->name() == "engine");
  CHECK(reg.find(nc::can_id::standard(0x200)) == nullptr);
}

TEST_CASE("registry: filters gate which frames are accepted") {
  auto const db{build_db()};
  nc::registry reg{db};
  reg.add_filter(nc::filter::equals(nc::can_id::standard(0x100)));

  CHECK(reg.accepts(nc::can_id::standard(0x100)));
  CHECK_FALSE(reg.accepts(nc::can_id::extended(0x18FEF100)));

  // match honours filters; find ignores them.
  std::array const payload{b(0x88), b(0x13), b(0x82)};
  auto const accepted{*nc::frame::classic(nc::can_id::standard(0x100), payload)};
  auto const blocked{*nc::frame::classic(nc::can_id::extended(0x18FEF100), std::array{b(0x10)})};
  CHECK(reg.match(accepted) != nullptr);
  CHECK(reg.match(blocked) == nullptr);
  CHECK(reg.find(blocked.id()) != nullptr);
}

TEST_CASE("registry: decode reports every signal of a matched frame") {
  auto const db{build_db()};
  nc::registry const reg{db};

  // speed raw 5000 -> 50.0 km/h, oil_temp raw 130 -> 90 C.
  std::array const payload{b(0x88), b(0x13), b(0x82)};
  auto const f{*nc::frame::classic(nc::can_id::standard(0x100), payload)};

  std::vector<std::pair<std::string, double>> seen;
  auto const count{reg.decode_signals(f, [&seen](nc::signal const& sig, double const value) {
    seen.emplace_back(std::string{sig.name()}, value);
  })};

  REQUIRE(count == 2);
  CHECK(seen[0].first == "speed");
  CHECK(seen[0].second == doctest::Approx(50.0));
  CHECK(seen[1].first == "oil_temp");
  CHECK(seen[1].second == doctest::Approx(90.0));
}

TEST_CASE("registry: decode of an unknown frame yields nothing") {
  auto const db{build_db()};
  nc::registry const reg{db};
  auto const f{*nc::frame::classic(nc::can_id::standard(0x7FF), std::array{b(0)})};
  auto const count{reg.decode_signals(f, [](nc::signal const&, double) {})};
  CHECK(count == 0);
}

TEST_CASE("registry: a signal past a short frame is skipped, not an error") {
  auto const db{build_db()};
  nc::registry const reg{db};
  // Only one byte: speed needs two, oil_temp needs three; both are skipped.
  auto const f{*nc::frame::classic(nc::can_id::standard(0x100), std::array{b(0x00)})};
  auto const count{reg.decode_signals(f, [](nc::signal const&, double) {})};
  CHECK(count == 0);
}

TEST_CASE("registry: an empty database resolves nothing") {
  nc::database const db;
  nc::registry const reg{db};
  CHECK(reg.find(nc::can_id::standard(0x1)) == nullptr);
  auto const f{*nc::frame::classic(nc::can_id::standard(0x1), std::array{b(0)})};
  CHECK(reg.decode_signals(f, [](nc::signal const&, double) {}) == 0);
}

TEST_CASE("registry: multiplexed signals decode only for the matching selector group") {
  // Byte 0 = selector; byte 1 = group 0 signal; byte 2 = group 1 signal.
  auto const message{nc::message_builder{nc::can_id::standard(0x200), "mux"}
                       .add(
                         nc::signal_builder{}
                           .name("sel")
                           .start_bit(0)
                           .length(8)
                           .mux_role(nc::multiplex_role::selector)
                           .build()
                       )
                       .add(
                         nc::signal_builder{}
                           .name("g0")
                           .start_bit(8)
                           .length(8)
                           .mux_role(nc::multiplex_role::multiplexed)
                           .mux_value(0)
                           .build()
                       )
                       .add(
                         nc::signal_builder{}
                           .name("g1")
                           .start_bit(16)
                           .length(8)
                           .mux_role(nc::multiplex_role::multiplexed)
                           .mux_value(1)
                           .build()
                       )
                       .build()};
  auto const db{nc::database_builder{}.add_message(message).build()};
  nc::registry const reg{db};

  // selector = 1 -> only sel and g1 decode.
  std::array const payload{b(0x01), b(0xAA), b(0x5A)};
  auto const f{*nc::frame::classic(nc::can_id::standard(0x200), payload)};

  std::vector<std::string> seen;
  auto const count{reg.decode_signals(f, [&seen](nc::signal const& sig, double) {
    seen.emplace_back(sig.name());
  })};
  CHECK(count == 2);
  CHECK(std::ranges::find(seen, "sel") != seen.end());
  CHECK(std::ranges::find(seen, "g1") != seen.end());
  CHECK(std::ranges::find(seen, "g0") == seen.end());
}

TEST_CASE("registry: a not-available signal is skipped during decode") {
  auto const message{nc::message_builder{nc::can_id::standard(0x250), "status"}
                       .add(nc::signal_builder{}.name("present").start_bit(0).length(8).build())
                       .add(
                         nc::signal_builder{}
                           .name("absent")
                           .start_bit(8)
                           .length(8)
                           .invalid(nc::invalid_value::all_ones)
                           .build()
                       )
                       .build()};
  auto const db{nc::database_builder{}.add_message(message).build()};
  nc::registry const reg{db};

  // byte 1 = 0xFF -> 'absent' is not available and is skipped; 'present' decodes.
  std::array const payload{b(0x2A), b(0xFF)};
  auto const f{*nc::frame::classic(nc::can_id::standard(0x250), payload)};

  std::vector<std::string> seen;
  auto const count{reg.decode_signals(f, [&seen](nc::signal const& sig, double) {
    seen.emplace_back(sig.name());
  })};
  CHECK(count == 1);
  CHECK(std::ranges::find(seen, "present") != seen.end());
  CHECK(std::ranges::find(seen, "absent") == seen.end());
}

}  // namespace
