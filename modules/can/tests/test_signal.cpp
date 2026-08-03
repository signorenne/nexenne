/**
 * @file
 * @brief Tests for the signal value type and its builder.
 */

#include <doctest/doctest.h>

#include <limits>

#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>

namespace {

namespace nc = nexenne::can;

TEST_CASE("signal: defaults are a 1-bit unsigned little-endian unscaled field") {
  constexpr nc::signal sig;
  static_assert(sig.start_bit() == 0);
  static_assert(sig.length() == 1);
  static_assert(sig.order() == nc::byte_order::little_endian);
  static_assert(!sig.is_signed());
  static_assert(sig.scale() == 1.0);
  static_assert(sig.offset() == 0.0);
  static_assert(sig.mux_role() == nc::multiplex_role::none);
  CHECK(sig.minimum() == -std::numeric_limits<double>::infinity());
  CHECK(sig.maximum() == std::numeric_limits<double>::infinity());
}

TEST_CASE("signal: mutable accessors set every field") {
  nc::signal sig;
  sig.start_bit() = 8;
  sig.length() = 16;
  sig.order() = nc::byte_order::big_endian;
  sig.is_signed() = true;
  sig.scale() = 0.5;
  sig.offset() = -10.0;
  sig.minimum() = -100.0;
  sig.maximum() = 100.0;
  sig.name() = "x";
  sig.unit() = "V";
  sig.mux_role() = nc::multiplex_role::multiplexed;
  sig.mux_value() = 3;

  CHECK(sig.start_bit() == 8);
  CHECK(sig.length() == 16);
  CHECK(sig.order() == nc::byte_order::big_endian);
  CHECK(sig.is_signed());
  CHECK(sig.scale() == doctest::Approx(0.5));
  CHECK(sig.offset() == doctest::Approx(-10.0));
  CHECK(sig.minimum() == doctest::Approx(-100.0));
  CHECK(sig.maximum() == doctest::Approx(100.0));
  CHECK(sig.name() == "x");
  CHECK(sig.unit() == "V");
  CHECK(sig.mux_role() == nc::multiplex_role::multiplexed);
  CHECK(sig.mux_value() == 3);
}

TEST_CASE("signal_builder: each setter populates exactly its field") {
  auto const sig{nc::signal_builder{}
                   .name("speed")
                   .start_bit(4)
                   .length(12)
                   .endianness(nc::byte_order::big_endian)
                   .is_signed(true)
                   .scale(0.25)
                   .offset(5.0)
                   .minimum(-50.0)
                   .maximum(50.0)
                   .unit("km/h")
                   .mux_role(nc::multiplex_role::selector)
                   .mux_value(7)
                   .build()};

  CHECK(sig.name() == "speed");
  CHECK(sig.start_bit() == 4);
  CHECK(sig.length() == 12);
  CHECK(sig.order() == nc::byte_order::big_endian);
  CHECK(sig.is_signed());
  CHECK(sig.scale() == doctest::Approx(0.25));
  CHECK(sig.offset() == doctest::Approx(5.0));
  CHECK(sig.minimum() == doctest::Approx(-50.0));
  CHECK(sig.maximum() == doctest::Approx(50.0));
  CHECK(sig.unit() == "km/h");
  CHECK(sig.mux_role() == nc::multiplex_role::selector);
  CHECK(sig.mux_value() == 7);
}

TEST_CASE("packing_plan::from_signal mirrors the signal's bit layout") {
  auto const sig{
    nc::signal_builder{}.start_bit(0).length(16).endianness(nc::byte_order::little_endian).build()
  };
  auto const plan{nc::packing_plan::from_signal(sig)};
  CHECK(plan.bit_length() == 16);
  CHECK_FALSE(plan.is_signed());
  CHECK(plan.required_length() == 2);
}

}  // namespace
