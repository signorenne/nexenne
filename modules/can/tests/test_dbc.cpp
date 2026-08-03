/**
 * @file
 * @brief Tests for the DBC text importer.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include <nexenne/can/codec.hpp>
#include <nexenne/can/dbc.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/registry.hpp>

namespace {

namespace nc = nexenne::can;

constexpr std::string_view kDbc{
  "VERSION \"\"\n"
  "\n"
  "BO_ 256 VehicleStatus: 8 ECU\n"
  " SG_ Speed : 0|16@1+ (0.01,0) [0|655.35] \"km/h\" Dash\n"
  " SG_ OilTemp : 16|8@1+ (1,-40) [-40|215] \"degC\" Dash\n"
  "\n"
  "BO_ 2566844672 EngineData: 8 ECU\n"  // 0x98FEF100: extended (top bit set)
  " SG_ Rpm : 0|16@1+ (0.125,0) [0|8031] \"rpm\" Dash\n"
};

TEST_CASE("dbc: parses messages and signals into a database") {
  auto const parsed{nc::parse_dbc(kDbc)};
  REQUIRE(parsed.has_value());
  CHECK(parsed->message_count() == 2);

  auto const* const status{parsed->find(nc::can_id::standard(256))};
  REQUIRE(status != nullptr);
  CHECK(status->name() == "VehicleStatus");
  CHECK(status->signal_count() == 2);
  REQUIRE(status->find_signal("Speed").has_value());
  auto const& speed{status->signals()[*status->find_signal("Speed")].definition};
  CHECK(speed.start_bit() == 0);
  CHECK(speed.length() == 16);
  CHECK(speed.order() == nc::byte_order::little_endian);
  CHECK(speed.scale() == doctest::Approx(0.01));
  CHECK(speed.unit() == "km/h");
}

TEST_CASE("dbc: a high-bit message id is parsed as extended") {
  auto const parsed{nc::parse_dbc(kDbc)};
  REQUIRE(parsed.has_value());
  auto const* const engine{parsed->find(nc::can_id::extended(0x18FEF100))};
  REQUIRE(engine != nullptr);
  CHECK(engine->name() == "EngineData");
  CHECK(engine->id().extended());
}

TEST_CASE("dbc: a big-endian signed signal is parsed") {
  auto const parsed{nc::parse_dbc("BO_ 16 M: 8 X\n SG_ S : 7|16@0- (1,0) [0|0] \"\" X\n")};
  REQUIRE(parsed.has_value());
  auto const& sig{parsed->messages()[0].signals()[0].definition};
  CHECK(sig.order() == nc::byte_order::big_endian);
  CHECK(sig.is_signed());
}

TEST_CASE("dbc: a parsed database decodes a frame end to end") {
  auto const parsed{nc::parse_dbc(kDbc)};
  REQUIRE(parsed.has_value());
  nc::registry const reg{parsed->db()};

  std::array const payload{std::byte{0x88}, std::byte{0x13}, std::byte{0x82}};  // 50.0 km/h, 90 C
  auto const f{*nc::frame::classic(nc::can_id::standard(256), payload)};
  std::size_t decoded{0};
  reg.decode_signals(f, [&decoded](nc::signal const&, double) { ++decoded; });
  CHECK(decoded == 2);
}

TEST_CASE("dbc: a malformed signal line is a parse error") {
  // Bind and check has_value() first: calling .error() on a valued expected is UB.
  auto const parsed{nc::parse_dbc("BO_ 1 M: 8 X\n SG_ Broken : not_a_layout\n")};
  REQUIRE_FALSE(parsed.has_value());
  CHECK(parsed.error() == nc::can_error::parse_error);
}

TEST_CASE("dbc: hostile numeric fields are rejected, never imported") {
  auto rejected = [](std::string_view const text) {
    auto const r{nc::parse_dbc(text)};
    return !r.has_value() && r.error() == nc::can_error::parse_error;
  };
  // Out-of-range signal length (would overflow the packing-plan chunk array).
  CHECK(rejected("BO_ 1 M: 8 X\n SG_ S : 0|200@1+ (1,0) [0|0] \"\" X\n"));
  // Length zero with a sign flag (would reach a shift-by-minus-one in decode).
  CHECK(rejected("BO_ 1 M: 8 X\n SG_ S : 0|0@1- (1,0) [0|0] \"\" X\n"));
  // Standard id above 0x7FF (would be silently masked to a different id).
  CHECK(rejected("BO_ 2049 M: 8 X\n SG_ S : 0|8@1+ (1,0) [0|0] \"\" X\n"));
  // Start bit above 16 bits (would truncate to a fabricated layout).
  CHECK(rejected("BO_ 1 M: 8 X\n SG_ S : 70000|8@1+ (1,0) [0|0] \"\" X\n"));
  // Non-finite scale.
  CHECK(rejected("BO_ 1 M: 8 X\n SG_ S : 0|8@1+ (inf,0) [0|0] \"\" X\n"));
  // Invalid byte-order character.
  CHECK(rejected("BO_ 1 M: 8 X\n SG_ S : 0|8@7+ (1,0) [0|0] \"\" X\n"));
  // A float value type on a signal whose width is not 32 or 64 bits is malformed.
  CHECK(rejected("BO_ 1 M: 8 X\n SG_ S : 0|8@1+ (1,0) [0|0] \"\" X\nSIG_VALTYPE_ 1 S : 1;\n"));
}

TEST_CASE("dbc: a [0|0] range means unbounded, not clamp-everything-to-zero") {
  // Regression: [0|0] is the DBC spelling for "no range specified". Treating it
  // as min=max=0 clamped every encoded value to 0.
  auto const parsed{nc::parse_dbc("BO_ 100 M: 8 X\n SG_ S : 0|16@1+ (1,0) [0|0] \"\" X\n")};
  REQUIRE(parsed.has_value());
  auto const* const msg{parsed->db().find(nc::can_id::standard(100))};
  REQUIRE(msg != nullptr);
  auto const& entry{msg->signals()[0]};
  auto frame{*nc::frame::classic(nc::can_id::standard(100), std::array<std::byte, 8>{})};
  REQUIRE(nc::encode(entry.definition, entry.plan, frame, 1234.0).has_value());
  CHECK(*nc::decode(entry.definition, entry.plan, frame) == doctest::Approx(1234.0));  // not 0
}

TEST_CASE("dbc: a float value type marks the signal, and float signals round-trip") {
  auto const parsed{nc::parse_dbc(
    "BO_ 100 Sensors: 8 ECU\n SG_ Temperature : 0|32@1- (1,0) [-100|100] \"degC\" ECU\n"
    "SIG_VALTYPE_ 100 Temperature : 1;\n"
  )};
  REQUIRE(parsed.has_value());
  auto const* const msg{parsed->db().find(nc::can_id::standard(100))};
  REQUIRE(msg != nullptr);
  REQUIRE(msg->signal_count() == 1);
  auto const& entry{msg->signals()[0]};
  CHECK(entry.definition.is_float());

  // Encode a float physical value and decode it back through the marked signal.
  auto frame{*nc::frame::classic(nc::can_id::standard(100), std::array<std::byte, 8>{})};
  REQUIRE(nc::encode(entry.definition, entry.plan, frame, 21.5).has_value());
  CHECK(*nc::decode(entry.definition, entry.plan, frame) == doctest::Approx(21.5));
}

TEST_CASE("dbc: the message byte length and extended multiplexing marker are imported") {
  auto const parsed{
    nc::parse_dbc("BO_ 100 Engine: 6 ECU\n SG_ Mode m2M : 0|8@1+ (1,0) [0|0] \"\" X\n")
  };
  REQUIRE(parsed.has_value());
  auto const* const msg{parsed->db().find(nc::can_id::standard(100))};
  REQUIRE(msg != nullptr);
  CHECK(msg->byte_length() == 6);     // the BO_ dlc is now retained
  REQUIRE(msg->signal_count() == 1);  // m2M no longer fails the whole file
  CHECK(msg->signals()[0].definition.mux_role() == nc::multiplex_role::multiplexed);
}

TEST_CASE("registry does not bind a temporary database") {
  static_assert(!std::is_constructible_v<nc::registry, nc::database&&>);
  static_assert(std::is_constructible_v<nc::registry, nc::database const&>);
}

TEST_CASE("dbc: value tables, comments, and attributes are parsed and queryable") {
  auto const parsed{nc::parse_dbc(
    "CM_ \"Vehicle database\";\n"
    "BO_ 256 Status: 8 ECU\n"
    " SG_ Gear : 0|4@1+ (1,0) [0|0] \"\" Dash\n"
    "CM_ BO_ 256 \"Powertrain status\";\n"
    "CM_ SG_ 256 Gear \"Current gear\";\n"
    "VAL_ 256 Gear 0 \"neutral\" 1 \"drive\" 2 \"reverse\" ;\n"
    "BA_ \"GenMsgCycleTime\" BO_ 256 100;\n"
    "BA_ \"GenSigStartValue\" SG_ 256 Gear 0;\n"
    "BA_ \"BusType\" \"CAN\";\n"
  )};
  REQUIRE(parsed.has_value());
  auto const id{nc::can_id::standard(256)};

  CHECK(parsed->database_comment() == "Vehicle database");
  CHECK(parsed->message_comment(id) == "Powertrain status");
  CHECK(parsed->signal_comment(id, "Gear") == "Current gear");

  REQUIRE(parsed->value_table(id, "Gear").size() == 3);
  CHECK(parsed->value_name(id, "Gear", 1) == "drive");
  CHECK(parsed->value_name(id, "Gear", 2) == "reverse");
  CHECK_FALSE(parsed->value_name(id, "Gear", 9).has_value());  // unnamed value

  CHECK(parsed->message_attribute(id, "GenMsgCycleTime") == "100");
  CHECK(parsed->signal_attribute(id, "Gear", "GenSigStartValue") == "0");
  CHECK(parsed->database_attribute("BusType") == "CAN");
  CHECK_FALSE(parsed->message_attribute(id, "Missing").has_value());
}

TEST_CASE("dbc: metadata views survive a move of the dbc_database") {
  auto parsed{nc::parse_dbc(
    "BO_ 256 Status: 8 ECU\n SG_ Gear : 0|4@1+ (1,0) [0|0] \"\" X\n"
    "VAL_ 256 Gear 1 \"drive\" ;\n"
  )};
  REQUIRE(parsed.has_value());
  auto const moved{std::move(*parsed)};  // move the dbc_database and its metadata
  CHECK(moved.value_name(nc::can_id::standard(256), "Gear", 1) == "drive");
}

TEST_CASE("dbc: names point into the retained source and survive a move") {
  auto parsed{nc::parse_dbc(kDbc)};
  REQUIRE(parsed.has_value());
  auto moved{std::move(*parsed)};  // move the dbc_database
  auto const* const status{moved.find(nc::can_id::standard(256))};
  REQUIRE(status != nullptr);
  CHECK(status->name() == "VehicleStatus");  // view still valid after the move
}

}  // namespace
