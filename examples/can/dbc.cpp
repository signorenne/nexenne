/**
 * @file
 * @brief dbc: importing a DBC database and decoding a frame with it.
 *
 * DBC is the standard text format for a CAN database. This example parses a small
 * DBC string into a database, prints what it found, and decodes a frame through a
 * registry built from it, the way a tool would load a vehicle's .dbc file.
 */

#include <array>
#include <cstddef>
#include <print>
#include <string_view>

#include <nexenne/can/dbc.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/registry.hpp>

namespace {

namespace nc = nexenne::can;

constexpr std::string_view kDbc{"CM_ \"Example vehicle database\";\n"
                                "BO_ 256 VehicleStatus: 8 ECU\n"
                                " SG_ Speed : 0|16@1+ (0.01,0) [0|655.35] \"km/h\" Dash\n"
                                " SG_ OilTemp : 16|8@1+ (1,-40) [-40|215] \"degC\" Dash\n"
                                " SG_ Gear : 24|4@1+ (1,0) [0|0] \"\" Dash\n"
                                "CM_ SG_ 256 Speed \"Vehicle road speed\";\n"
                                "VAL_ 256 Gear 0 \"neutral\" 1 \"drive\" 2 \"reverse\" ;\n"
                                "BA_ \"GenMsgCycleTime\" BO_ 256 100;\n"};

}  // namespace

auto main() -> int {
  auto const parsed{nc::parse_dbc(kDbc)};
  if (!parsed) {
    std::println("DBC parse failed: {}", nc::to_string(parsed.error()));
    return 1;
  }
  std::println("database comment: {}", parsed->database_comment());
  std::println("loaded {} message(s):", parsed->message_count());
  for (nc::message const& message : parsed->messages()) {
    std::println("  {}", message);
    for (nc::signal_entry const& entry : message.signals()) {
      std::println("    {}", entry.definition);
    }
  }

  // Descriptive metadata: comments, value tables, and attributes.
  auto const status{nc::can_id::standard(256)};
  std::println("Speed comment: {}", parsed->signal_comment(status, "Speed"));
  std::println(
    "message cycle time: {} ms", parsed->message_attribute(status, "GenMsgCycleTime").value_or("?")
  );
  std::println("Gear value table:");
  for (nc::dbc_enum_value const& named : parsed->value_table(status, "Gear")) {
    std::println("  {}", named);
  }

  // Decode a frame using the imported database.
  nc::registry const reg{parsed->db()};
  std::array const payload{std::byte{0x88}, std::byte{0x13}, std::byte{0x82}};
  auto const f{*nc::frame::classic(nc::can_id::standard(256), payload)};
  std::println("decoding {}:", f);
  reg.decode_signals(f, [](nc::signal const& sig, double const value) {
    std::println("  {} = {} {}", sig.name(), value, sig.unit());
  });

  return 0;
}
