/**
 * @file
 * @brief Tests for packing and unpacking signal values through a frame.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <span>

#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/codec.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto b(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

auto make_frame(std::span<std::byte const> const payload) -> nc::frame {
  return *nc::frame::classic(nc::can_id::standard(0x100), payload);
}

TEST_CASE("unpack: unsigned field with a scale factor") {
  nc::signal const sig{0, 16, nc::byte_order::little_endian, false, 0.01, 0.0};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const payload{b(0x88), b(0x13)};  // 0x1388 = 5000
  auto const value{nc::decode(sig, plan, make_frame(payload))};
  REQUIRE(value.has_value());
  CHECK(*value == doctest::Approx(50.0));
}

TEST_CASE("unpack: signed field sign-extends two's complement") {
  nc::signal const sig{0, 8, nc::byte_order::little_endian, true, 1.0, 0.0};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const minus_one{b(0xFF)};
  std::array const minus_full{b(0x80)};  // -128 in 8-bit two's complement
  CHECK(*nc::decode(sig, plan, make_frame(minus_one)) == doctest::Approx(-1.0));
  CHECK(*nc::decode(sig, plan, make_frame(minus_full)) == doctest::Approx(-128.0));
}

TEST_CASE("pack then unpack round-trips a scaled value") {
  nc::signal const sig{0, 16, nc::byte_order::little_endian, false, 0.01, 0.0};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const zeros{b(0), b(0)};
  auto frame{make_frame(zeros)};

  REQUIRE(nc::encode(sig, plan, frame, 50.0).has_value());
  CHECK(frame.data()[0] == b(0x88));  // 5000 little-endian
  CHECK(frame.data()[1] == b(0x13));
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(50.0));
}

TEST_CASE("pack: an offset is applied, here a temperature with -40 bias") {
  nc::signal const sig{0, 8, nc::byte_order::little_endian, false, 1.0, -40.0};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const zero{b(0)};
  auto frame{make_frame(zero)};

  REQUIRE(nc::encode(sig, plan, frame, 90.0).has_value());  // raw = 90 - (-40) = 130
  CHECK(frame.data()[0] == b(0x82));
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(90.0));
}

TEST_CASE("encode: a value above the range is clamped, not rejected") {
  auto const sig{nc::signal_builder{}
                   .start_bit(0)
                   .length(8)
                   .endianness(nc::byte_order::little_endian)
                   .minimum(0.0)
                   .maximum(200.0)
                   .build()};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const zero{b(0)};
  auto frame{make_frame(zero)};

  REQUIRE(nc::encode(sig, plan, frame, 300.0).has_value());
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(200.0));
}

TEST_CASE("pack: a value that overflows the field width is rejected") {
  nc::signal const sig{0, 8, nc::byte_order::little_endian, false, 1.0, 0.0};  // unbounded range
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const zero{b(0)};
  auto frame{make_frame(zero)};

  auto const r{nc::encode(sig, plan, frame, 300.0)};  // 300 does not fit 8 bits
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == nc::can_error::value_out_of_range);
}

TEST_CASE("a field running past the frame length is out of range") {
  nc::signal const sig{0, 16, nc::byte_order::little_endian, false};  // needs 2 bytes
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const one_byte{b(0x00)};
  auto frame{make_frame(one_byte)};  // only 1 byte

  CHECK(nc::decode(sig, plan, frame).error() == nc::can_error::signal_out_of_range);
  CHECK(nc::encode(sig, plan, frame, 1.0).error() == nc::can_error::signal_out_of_range);
}

TEST_CASE("raw helpers move the integer field without scaling") {
  nc::packing_plan const plan{8, 8, nc::byte_order::little_endian, false};
  std::array const zeros{b(0), b(0)};
  auto frame{make_frame(zeros)};

  REQUIRE(nc::write_bits(plan, frame, 0x5A).has_value());
  CHECK(frame.data()[1] == b(0x5A));
  CHECK(*nc::read_bits(plan, frame) == 0x5A);
}

TEST_CASE("not available: the all-ones sentinel writes and reads back") {
  nc::packing_plan const plan{0, 8, nc::byte_order::little_endian, false};
  std::array const zero{b(0)};
  auto frame{make_frame(zero)};

  CHECK(nc::not_available_value(plan) == 0xFF);
  CHECK_FALSE(*nc::is_not_available(plan, frame));
  REQUIRE(nc::write_not_available(plan, frame).has_value());
  CHECK(frame.data()[0] == b(0xFF));
  CHECK(*nc::is_not_available(plan, frame));
}

TEST_CASE("codec: a full 64-bit signed field round-trips the extremes") {
  nc::signal const sig{0, 64, nc::byte_order::little_endian, true, 1.0, 0.0};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array<std::byte, 8> bytes{};
  auto frame{make_frame(bytes)};

  REQUIRE(nc::encode(sig, plan, frame, -1.0).has_value());
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(-1.0));

  // -2^63 and 2^63 - 1 are the signed 64-bit extremes; both must fit.
  double const lowest{-9223372036854775808.0};
  REQUIRE(nc::encode(sig, plan, frame, lowest).has_value());
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(lowest));
}

TEST_CASE("codec: a signed big-endian field with scale and offset round-trips") {
  nc::signal const sig{7, 16, nc::byte_order::big_endian, true, 0.1, -50.0};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const zeros{b(0), b(0)};
  auto frame{make_frame(zeros)};

  REQUIRE(nc::encode(sig, plan, frame, -12.3).has_value());
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(-12.3));
}

TEST_CASE("encode: a signed value out of the field range is rejected at both ends") {
  nc::signal const sig{0, 8, nc::byte_order::little_endian, true};  // range [-128, 127]
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const zero{b(0)};
  auto frame{make_frame(zero)};

  CHECK(nc::encode(sig, plan, frame, 200.0).error() == nc::can_error::value_out_of_range);
  CHECK(nc::encode(sig, plan, frame, -200.0).error() == nc::can_error::value_out_of_range);
  CHECK(nc::encode(sig, plan, frame, 127.0).has_value());
  CHECK(nc::encode(sig, plan, frame, -128.0).has_value());
}

TEST_CASE("encode: a negative clamp range pins values to the minimum") {
  auto const sig{
    nc::signal_builder{}.start_bit(0).length(8).is_signed(true).minimum(-50.0).maximum(50.0).build()
  };
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array const zero{b(0)};
  auto frame{make_frame(zero)};

  REQUIRE(nc::encode(sig, plan, frame, -100.0).has_value());  // clamped to -50
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(-50.0));
}

TEST_CASE("decode_value: a 'none' policy always yields a value") {
  auto const sig{
    nc::signal_builder{}.start_bit(0).length(8).invalid(nc::invalid_value::none).build()
  };
  auto const plan{nc::packing_plan::from_signal(sig)};
  auto const result{nc::decode_value(sig, plan, make_frame(std::array{b(0xFF)}))};
  REQUIRE(result.has_value());
  REQUIRE(result->has_value());
  CHECK(**result == doctest::Approx(255.0));
}

TEST_CASE("decode_value: the all-ones policy reports 0xFF as not available") {
  auto const sig{
    nc::signal_builder{}.start_bit(0).length(8).invalid(nc::invalid_value::all_ones).build()
  };
  auto const plan{nc::packing_plan::from_signal(sig)};
  CHECK_FALSE(nc::decode_value(sig, plan, make_frame(std::array{b(0xFF)}))->has_value());
  CHECK(nc::decode_value(sig, plan, make_frame(std::array{b(0x00)}))->has_value());  // 0 is real
  CHECK(**nc::decode_value(sig, plan, make_frame(std::array{b(0x10)})) == doctest::Approx(16.0));
}

TEST_CASE("decode_value: the all-zeros and both policies") {
  auto const zeros{
    nc::signal_builder{}.start_bit(0).length(8).invalid(nc::invalid_value::all_zeros).build()
  };
  auto const both{nc::signal_builder{}
                    .start_bit(0)
                    .length(8)
                    .invalid(nc::invalid_value::all_ones_or_zeros)
                    .build()};
  auto const zplan{nc::packing_plan::from_signal(zeros)};
  auto const bplan{nc::packing_plan::from_signal(both)};

  CHECK_FALSE(nc::decode_value(zeros, zplan, make_frame(std::array{b(0x00)}))->has_value());
  CHECK(nc::decode_value(zeros, zplan, make_frame(std::array{b(0xFF)}))->has_value());

  CHECK_FALSE(nc::decode_value(both, bplan, make_frame(std::array{b(0x00)}))->has_value());
  CHECK_FALSE(nc::decode_value(both, bplan, make_frame(std::array{b(0xFF)}))->has_value());
  CHECK(nc::decode_value(both, bplan, make_frame(std::array{b(0x40)}))->has_value());
}

TEST_CASE("encode: a finite but out-of-range value is rejected, never written as garbage") {
  // Regression: encode checked only isfinite before llround, whose result is
  // unspecified out of [LLONG_MIN, LLONG_MAX]. A signed 64-bit field accepted
  // 1e30 and wrote 0x8000000000000000 (decoded as -9.22e18) with success.
  nc::signal const sig{0, 64, nc::byte_order::little_endian, true, 1.0, 0.0};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array<std::byte, 8> bytes{};
  auto frame{make_frame(bytes)};

  CHECK(nc::encode(sig, plan, frame, 1e30).error() == nc::can_error::value_out_of_range);
  CHECK(
    nc::encode(sig, plan, frame, 9223372036854775808.0).error()  // exactly 2^63
    == nc::can_error::value_out_of_range
  );
  // The frame stays untouched by the rejected writes.
  CHECK(frame.data()[7] == b(0x00));
}

TEST_CASE("encode: an unsigned 64-bit field can reach values above INT64_MAX") {
  // Regression: the unsigned path routed through llround -> long long, so raw
  // values in (INT64_MAX, UINT64_MAX] were rejected although they fit the field.
  nc::signal const sig{0, 64, nc::byte_order::little_endian, false, 1.0, 0.0};
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array<std::byte, 8> bytes{};
  auto frame{make_frame(bytes)};

  constexpr double big{1.2e19};  // < 2^64, above 2^63
  REQUIRE(nc::encode(sig, plan, frame, big).has_value());
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(big));

  // Just beyond the field is still rejected.
  CHECK(nc::encode(sig, plan, frame, 1.9e19).error() == nc::can_error::value_out_of_range);
}

TEST_CASE("codec: a float signal encodes and decodes IEEE-754 bits") {
  nc::signal sig32{0, 32, nc::byte_order::little_endian, false};
  sig32.is_float() = true;
  auto const plan32{nc::packing_plan::from_signal(sig32)};
  std::array<std::byte, 8> bytes{};
  auto frame{make_frame(bytes)};
  REQUIRE(nc::encode(sig32, plan32, frame, 3.5).has_value());
  CHECK(*nc::decode(sig32, plan32, frame) == doctest::Approx(3.5));
  REQUIRE(nc::encode(sig32, plan32, frame, -1.25).has_value());
  CHECK(*nc::decode(sig32, plan32, frame) == doctest::Approx(-1.25));

  nc::signal sig64{0, 64, nc::byte_order::little_endian, false};
  sig64.is_float() = true;
  auto const plan64{nc::packing_plan::from_signal(sig64)};
  auto frame64{make_frame(bytes)};
  REQUIRE(nc::encode(sig64, plan64, frame64, 1.0e-9).has_value());
  CHECK(*nc::decode(sig64, plan64, frame64) == doctest::Approx(1.0e-9));
}

TEST_CASE("codec: encode_strict rejects an out-of-range value instead of clamping") {
  nc::signal sig{0, 8, nc::byte_order::little_endian, false, 1.0, 0.0};
  sig.minimum() = 0.0;
  sig.maximum() = 200.0;
  auto const plan{nc::packing_plan::from_signal(sig)};
  std::array<std::byte, 8> bytes{};
  auto frame{make_frame(bytes)};

  // encode clamps 300 -> 200; encode_strict rejects it.
  REQUIRE(nc::encode(sig, plan, frame, 300.0).has_value());
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(200.0));
  CHECK(nc::encode_strict(sig, plan, frame, 300.0).error() == nc::can_error::value_out_of_range);
  // An in-range value goes through both.
  REQUIRE(nc::encode_strict(sig, plan, frame, 100.0).has_value());
  CHECK(*nc::decode(sig, plan, frame) == doctest::Approx(100.0));
}

}  // namespace
