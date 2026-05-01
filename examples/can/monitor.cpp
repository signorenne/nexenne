/**
 * @file
 * @brief monitor: receive frames off a bus and get notified of new and changed signals.
 *
 * This is the end-to-end showcase: it builds a small vehicle database, runs a
 * \c signal_monitor that polls a bus, decodes every received frame against the
 * database, and fires callbacks, one when a frame arrives and one when a named
 * signal is first seen or changes value. A simulated set of ECUs publishes frames
 * over a few ticks so there is real traffic to react to.
 *
 * The monitor here is a self-contained reusable piece: it works with any backend
 * that satisfies the \c can_bus concept. This example drives it with the
 * hardware-free \c loopback_bus, but swapping in the Linux \c socketcan_bus (see
 * the note in \c main) turns it into a live bus monitor with no other changes.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/can/codec.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/loopback_bus.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/registry.hpp>
#include <nexenne/can/signal_builder.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace nc = nexenne::can;

// Identifiers the simulated ECUs transmit on.
constexpr std::uint16_t engine_id{0x100};
constexpr std::uint16_t chassis_id{0x200};
constexpr std::uint16_t unknown_id{0x400};  // deliberately absent from the database

// Builds the vehicle database the monitor decodes against: two messages, each
// with a few signals, including a boolean flag and a not-available fuel reading.
auto build_database() -> nc::database {
  auto const engine_rpm{nc::signal_builder{}
                          .name("engine_rpm")
                          .start_bit(0)
                          .length(16)
                          .endianness(nc::byte_order::little_endian)
                          .scale(0.25)
                          .unit("rpm")
                          .build()};
  auto const coolant_temp{nc::signal_builder{}
                            .name("coolant_temp")
                            .start_bit(16)
                            .length(8)
                            .endianness(nc::byte_order::little_endian)
                            .offset(-40.0)
                            .unit("degC")
                            .build()};
  auto const engine_running{nc::signal_builder{}
                              .name("engine_running")
                              .start_bit(24)
                              .length(1)
                              .endianness(nc::byte_order::little_endian)
                              .build()};

  auto const vehicle_speed{nc::signal_builder{}
                             .name("vehicle_speed")
                             .start_bit(0)
                             .length(16)
                             .endianness(nc::byte_order::little_endian)
                             .scale(0.01)
                             .unit("km/h")
                             .build()};
  auto const brake_active{nc::signal_builder{}
                            .name("brake_active")
                            .start_bit(16)
                            .length(1)
                            .endianness(nc::byte_order::little_endian)
                            .build()};
  // All-ones means "not available", so a 0xFF raw reading is skipped on decode.
  auto const fuel_level{nc::signal_builder{}
                          .name("fuel_level")
                          .start_bit(24)
                          .length(8)
                          .endianness(nc::byte_order::little_endian)
                          .scale(0.5)
                          .unit("%")
                          .invalid(nc::invalid_value::all_ones)
                          .build()};

  return nc::database_builder{}
    .add_message(nc::message_builder{nc::can_id::standard(engine_id), "engine"}
                   .add(engine_rpm)
                   .add(coolant_temp)
                   .add(engine_running)
                   .build())
    .add_message(nc::message_builder{nc::can_id::standard(chassis_id), "chassis"}
                   .add(vehicle_speed)
                   .add(brake_active)
                   .add(fuel_level)
                   .build())
    .build();
}

/**
 * @brief Decodes received frames against a database and notifies on new data.
 *
 * Poll it with a bus; for every frame it dequeues, it looks the frame up in the
 * registry, reports it through \c on_frame, decodes each signal, and reports a
 * signal through \c on_signal only when its value is first seen or has changed
 * since last time. A frame with no database entry is reported through
 * \c on_unknown. This mirrors a typical CAN monitor: traffic is always visible,
 * but value notifications fire only on change.
 */
class signal_monitor {
public:
  /// @brief How a decoded signal value relates to the last value seen for it.
  enum class value_change {
    first_seen,
    changed,
    unchanged
  };

  /// @brief Called once per received frame that matches a database message.
  std::function<void(nc::message const&, nc::frame const&)> on_frame;
  /// @brief Called per decoded signal with how its value changed.
  std::function<void(nc::message const&, nc::signal const&, double, value_change)> on_signal;
  /// @brief Called per received frame with no matching database message.
  std::function<void(nc::frame const&)> on_unknown;

  explicit signal_monitor(nc::registry const& reg) noexcept : m_registry{&reg} {}

  /**
   * @brief Drains every frame waiting on \p bus, firing the callbacks.
   *
   * @tparam Bus A type satisfying the \c can_bus concept.
   * @param bus Bus to receive from until it has no frame ready.
   *
   * @return The number of frames processed.
   */
  template <typename Bus>
  auto poll(Bus& bus) -> std::size_t {
    std::size_t processed{0};
    while (true) {
      auto const received{bus.receive()};
      if (!received || !received->has_value()) {
        break;  // an I/O error or an empty queue both end this poll
      }
      handle(**received);
      ++processed;
    }
    return processed;
  }

private:
  nc::registry const* m_registry;
  std::map<std::string, double> m_last_value;  // "message::signal" -> last decoded value

  auto handle(nc::frame const& f) -> void {
    nc::message const* const msg{m_registry->match(f)};
    if (msg == nullptr) {
      if (on_unknown) {
        on_unknown(f);
      }
      return;
    }
    if (on_frame) {
      on_frame(*msg, f);
    }
    m_registry->decode_signals(f, [&](nc::signal const& sig, double const value) {
      std::string key{std::string{msg->name()} + "::" + std::string{sig.name()}};
      auto const it{m_last_value.find(key)};
      value_change const change{
        it == m_last_value.end() ? value_change::first_seen
        : it->second != value    ? value_change::changed
                                 : value_change::unchanged
      };
      m_last_value[key] = value;
      if (on_signal) {
        on_signal(*msg, sig, value, change);
      }
    });
  }
};

// Encodes named physical values into a frame and sends it, the way an ECU would
// publish a message. A NaN value writes the signal's not-available sentinel.
auto publish(
  nc::loopback_bus<>& bus,
  nc::registry const& reg,
  nc::can_id const id,
  std::initializer_list<std::pair<std::string_view, double>> const values,
  std::uint64_t const timestamp_ns
) -> void {
  nc::message const* const msg{reg.find(id)};
  if (msg == nullptr) {
    return;
  }
  std::array<std::byte, 8> const bytes{};
  auto built{nc::frame::classic(id, bytes)};
  if (!built) {
    return;
  }
  nc::frame f{*built};
  f.timestamp_ns() = timestamp_ns;
  for (auto const& [name, value] : values) {
    for (nc::signal_entry const& entry : msg->signals()) {
      if (entry.definition.name() != name) {
        continue;
      }
      if (std::isnan(value)) {
        nexenne::utility::discard(nc::write_not_available(entry.plan, f));
      } else {
        nexenne::utility::discard(nc::encode(entry.definition, entry.plan, f, value));
      }
    }
  }
  nexenne::utility::discard(bus.send(f));
}

auto change_tag(signal_monitor::value_change const change) -> std::string_view {
  switch (change) {
    case signal_monitor::value_change::first_seen:
      return "new";
    case signal_monitor::value_change::changed:
      return "changed";
    case signal_monitor::value_change::unchanged:
      return "";
  }
  return "";
}

}  // namespace

auto main() -> int {
  auto const db{build_database()};
  nc::registry const reg{db};

  // Swap this one line for a nc::socketcan_bus bus{"can0"} on Linux to monitor a
  // real interface; everything below is unchanged because both satisfy can_bus.
  nc::loopback_bus<> bus;

  signal_monitor monitor{reg};
  monitor.on_frame = [](nc::message const& msg, nc::frame const& f) {
    std::println(
      "[{:>4}ms] rx {} (0x{:03X})", f.timestamp_ns() / 1'000'000, msg.name(), f.id().identifier()
    );
  };
  monitor.on_signal = [](
                        nc::message const&,
                        nc::signal const& sig,
                        double const value,
                        signal_monitor::value_change const change
                      ) {
    if (change == signal_monitor::value_change::unchanged) {
      return;  // notify only on first-seen and changed values, like a real monitor
    }
    std::println("           {} = {:g} {} ({})", sig.name(), value, sig.unit(), change_tag(change));
  };
  monitor.on_unknown = [](nc::frame const& f) {
    std::println(
      "[{:>4}ms] rx 0x{:03X} (unknown, no database entry)",
      f.timestamp_ns() / 1'000'000,
      f.id().identifier()
    );
  };

  // A few ticks of simulated ECU traffic: some values change, some hold steady,
  // one fuel reading is not available, and one frame is from an unknown id.
  auto const engine{nc::can_id::standard(engine_id)};
  auto const chassis{nc::can_id::standard(chassis_id)};
  constexpr double not_available{std::numeric_limits<double>::quiet_NaN()};

  std::println("-- tick 1: engine starts, vehicle stationary --");
  publish(
    bus,
    reg,
    engine,
    {{"engine_rpm", 800}, {"coolant_temp", 20}, {"engine_running", 1}},
    100'000'000
  );
  publish(
    bus, reg, chassis, {{"vehicle_speed", 0}, {"brake_active", 1}, {"fuel_level", 60}}, 110'000'000
  );
  monitor.poll(bus);

  std::println("-- tick 2: revving and rolling, fuel sensor drops out --");
  publish(
    bus,
    reg,
    engine,
    {{"engine_rpm", 1500}, {"coolant_temp", 20}, {"engine_running", 1}},
    200'000'000
  );
  publish(
    bus,
    reg,
    chassis,
    {{"vehicle_speed", 12.5}, {"brake_active", 0}, {"fuel_level", not_available}},
    210'000'000
  );
  monitor.poll(bus);

  std::println("-- tick 3: warmed up, an unknown node speaks --");
  publish(
    bus,
    reg,
    engine,
    {{"engine_rpm", 1500}, {"coolant_temp", 31}, {"engine_running", 1}},
    300'000'000
  );
  {
    std::array<std::byte, 2> const payload{std::byte{0xDE}, std::byte{0xAD}};
    auto unknown{nc::frame::classic(nc::can_id::standard(unknown_id), payload)};
    if (unknown) {
      unknown->timestamp_ns() = 305'000'000;
      nexenne::utility::discard(bus.send(*unknown));
    }
  }
  monitor.poll(bus);

  return 0;
}
