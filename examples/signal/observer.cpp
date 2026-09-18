/**
 * @file
 * @brief Publish/subscribe with nexenne::signal, from priority to aggregation.
 *
 * A sensor publishes readings through a signal exposed as a connect-only sink.
 * Subscribers connect with a member function tracked by a slot (so they
 * auto-disconnect when they die), a one-shot calibration hook, and a
 * priority-ordered logger. The example also shows a scoped_connection, an
 * emit_blocker, explicit handle disconnect, disconnect_all, and a separate
 * non-void signal aggregated with emit_and_collect.
 *
 * The program walks four steps:
 *
 * 1. Three readings reach a logger at priority -1 (lower fires first), a
 *    calibration hook that fires on the first reading only, and a display
 *    whose slot member drops its subscription when the display expires, so a
 *    fourth reading reaches the logger alone. Clients hold only the sensor's
 *    sink, so they can subscribe but never fire readings themselves.
 * 2. A directly-owned alarm shows scoped_connection (RAII disconnect) and
 *    emit_blocker (scoped, save-and-restore emission suppression).
 * 3. A connection is a value handle: keep it and disconnect explicitly when a
 *    subscription's life is neither a scope nor an object; disconnect_all clears
 *    the whole signal at once, a "tear down the UI" moment, and strands any
 *    handle still held, whose disconnect then does nothing.
 * 4. A non-void signal polls every responder with emit_and_collect, gathering
 *    the answers in fire order, and folds them into a tally with no central
 *    response table to maintain.
 *
 * For the full event-driven walkthrough see showcase.cpp; this file is the
 * focused publish/subscribe tour.
 */

#include <print>
#include <vector>

#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/signal.hpp>
#include <nexenne/signal/slot.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace sg = nexenne::signal;

/// @brief A temperature sensor that publishes readings through a connect-only sink.
class sensor {
public:
  [[nodiscard]] auto readings() noexcept -> sg::sink<void(double)> {
    return m_on_reading.as_sink();
  }

  auto publish(double const celsius) noexcept -> void {
    m_on_reading.emit(celsius);
  }

private:
  sg::signal<void(double)> m_on_reading{};
};

/// @brief A subscriber whose slot member ties its subscription to its own lifetime.
class display {
public:
  explicit display(sg::sink<void(double)> source) noexcept {
    source.connect<&display::show>(*this, m_slot);
  }

  auto show(double const celsius) noexcept -> void {
    std::println("  display: {:.1f} C", celsius);
  }

private:
  sg::slot<2> m_slot{};  ///< Disconnects every tracked subscription on destruction.
};

}  // namespace

auto main() -> int {
  auto temp{sensor{}};

  [[maybe_unused]] auto log{temp.readings().connect(
    [](double const c) noexcept { std::println("  log[prio -1]: {:.1f} C", c); }, -1
  )};

  [[maybe_unused]] auto calib{temp.readings().connect_once([](double const c) noexcept {
    std::println("  calibrate once at {:.1f} C", c);
  })};

  {
    auto screen{display{temp.readings()}};

    std::println("first reading:");
    temp.publish(21.0);

    std::println("second reading:");
    temp.publish(22.5);

    std::println("third reading (display about to expire):");
    temp.publish(23.0);
  }

  std::println("after the display expired:");
  temp.publish(24.0);

  auto alarm{sg::signal<void()>{}};
  {
    auto const beep{sg::scoped_connection{alarm.connect([] noexcept { std::println("  beep"); })}};
    std::println("\nalarm with a scoped subscriber:");
    alarm.emit();
    {
      auto const quiet{sg::emit_blocker{alarm}};
      std::println("blocked scope (no beep follows):");
      alarm.emit();
    }
    std::println("unblocked again:");
    alarm.emit();
  }
  std::println("after the scoped subscriber left:");
  alarm.emit();

  std::println("\nexplicit disconnect and disconnect_all:");
  auto chime{alarm.connect([] noexcept { std::println("  chime"); })};
  [[maybe_unused]] auto buzz{alarm.connect([] noexcept { std::println("  buzz"); })};
  std::println("both connected ({} slots):", alarm.size());
  alarm.emit();
  std::println("disconnect chime explicitly: {}", chime.disconnect());
  alarm.emit();
  alarm.disconnect_all();
  std::println("after disconnect_all, empty: {}", alarm.empty());

  std::println("\nvote tally via emit_and_collect:");
  auto poll{sg::signal<bool(int)>{}};
  [[maybe_unused]] auto const a{poll.connect([](int n) noexcept { return n > 0; })};
  [[maybe_unused]] auto const b{poll.connect([](int n) noexcept { return n % 2 == 0; })};
  [[maybe_unused]] auto const c{poll.connect([](int n) noexcept { return n < 100; })};
  std::vector<bool> const votes{poll.emit_and_collect(42)};
  auto yes{0};
  for (bool const v : votes) {
    yes += v ? 1 : 0;
  }
  std::println("  {} of {} responders said yes for 42", yes, votes.size());
}
