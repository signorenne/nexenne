/**
 * @file
 * @brief Publish/subscribe with nexenne::signal: priority, one-shot, lifetime
 *        tracking, scoped and blocked connections, return aggregation.
 *
 * A sensor publishes readings through a signal exposed as a connect-only sink.
 * Subscribers connect with a member function tracked by a slot (so they
 * auto-disconnect when they die), a one-shot calibration hook, and a
 * priority-ordered logger. The example also shows a scoped_connection, an
 * emit_blocker, explicit handle disconnect, disconnect_all, and a separate
 * non-void signal aggregated with emit_and_collect.
 *
 * For the full event-driven walkthrough see showcase.cpp; this file is the
 * focused publish/subscribe tour.
 */

#include <print>
#include <vector>

#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/signal.hpp>
#include <nexenne/signal/slot.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace sg = nexenne::signal;

// A sensor owns the signal and only hands out a connect-only sink, so clients
// can subscribe but cannot fire readings themselves.
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

// A subscriber that ties its subscription to its own lifetime via a slot.
class display {
public:
  explicit display(sg::sink<void(double)> source) noexcept {
    source.connect<&display::show>(*this, m_slot);
  }

  auto show(double const celsius) noexcept -> void {
    std::println("  display: {:.1f} C", celsius);
  }

private:
  sg::slot<2> m_slot{};  // auto-disconnects every tracked subscription on death
};

}  // namespace

auto main() -> int {
  auto temp{sensor{}};

  // A priority-ordered logger: lower priority fires first.
  auto log{temp.readings().connect(
    [](double const c) noexcept { std::println("  log[prio -1]: {:.1f} C", c); }, -1
  )};

  // A one-shot calibration hook that fires only on the first reading.
  auto calib{temp.readings().connect_once([](double const c) noexcept {
    std::println("  calibrate once at {:.1f} C", c);
  })};

  {
    auto screen{display{temp.readings()}};

    std::println("first reading:");
    temp.publish(21.0);  // log, calibrate-once, then display

    std::println("second reading:");
    temp.publish(22.5);  // log and display; the one-shot is gone

    std::println("third reading (display about to expire):");
    temp.publish(23.0);
  }  // screen dies here; its slot auto-disconnects the tracked subscription

  std::println("after the display expired:");
  temp.publish(24.0);  // only the logger remains

  nexenne::utility::discard(log);
  nexenne::utility::discard(calib);

  // A directly-owned signal shows scoped_connection (RAII disconnect) and
  // emit_blocker (scoped, save-and-restore emission suppression).
  auto alarm{sg::signal<void()>{}};
  {
    auto const beep{sg::scoped_connection{alarm.connect([] noexcept { std::println("  beep"); })}};
    std::println("\nalarm with a scoped subscriber:");
    alarm.emit();  // beeps
    {
      auto const quiet{sg::emit_blocker{alarm}};
      std::println("blocked scope (no beep follows):");
      alarm.emit();  // suppressed, prints nothing
    }
    std::println("unblocked again:");
    alarm.emit();  // beeps
  }  // beep's scoped_connection disconnects here
  std::println("after the scoped subscriber left:");
  alarm.emit();  // nothing connected

  // A connection is a value handle: keep it and disconnect explicitly when the
  // subscription's life is neither a scope nor an object, and disconnect_all to
  // clear the whole signal at once (here, a "tear down the UI" moment).
  std::println("\nexplicit disconnect and disconnect_all:");
  auto chime{alarm.connect([] noexcept { std::println("  chime"); })};
  auto buzz{alarm.connect([] noexcept { std::println("  buzz"); })};
  std::println("both connected ({} slots):", alarm.size());
  alarm.emit();  // chime, buzz
  std::println("disconnect chime explicitly: {}", chime.disconnect());
  alarm.emit();  // buzz only
  alarm.disconnect_all();
  std::println("after disconnect_all, empty: {}", alarm.empty());
  nexenne::utility::discard(buzz);  // its slot is already gone; the handle now no-ops

  // A non-void signal: each slot returns a value and emit_and_collect gathers
  // them in fire order. A poll like this fans one query out to every registered
  // responder, then folds the answers - no central response table to maintain.
  std::println("\nvote tally via emit_and_collect:");
  auto poll{sg::signal<bool(int)>{}};
  auto const a{poll.connect([](int n) noexcept { return n > 0; })};
  auto const b{poll.connect([](int n) noexcept { return n % 2 == 0; })};
  auto const c{poll.connect([](int n) noexcept { return n < 100; })};
  nexenne::utility::discard(a);
  nexenne::utility::discard(b);
  nexenne::utility::discard(c);
  std::vector<bool> const votes{poll.emit_and_collect(42)};
  auto yes{0};
  for (bool const v : votes) {
    yes += v ? 1 : 0;
  }
  std::println("  {} of {} responders said yes for 42", yes, votes.size());
}
