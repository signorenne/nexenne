/**
 * @file
 * @brief Heap-free pub/sub with nexenne::signal::static_signal.
 *
 * A fixed-capacity signal that allocates nothing: the slot storage lives inline
 * in the signal. Shows capacity handling (a connect past the bound fails rather
 * than allocating), priority ordering, lifetime tracking with static_slot, a
 * connect-only sink, a one-shot slot, an emit_blocker, and a reentrant
 * disconnect from inside a running slot.
 *
 * The program walks three steps:
 *
 * 1. A sensor owns a 4-slot signal and hands out a connect-only sink. A logger
 *    at priority -1 fires before a display whose static_slot tracker drops its
 *    subscription when the display expires, leaving the logger alone.
 * 2. Capacity is fixed: on a 2-slot signal the third connect fails with an
 *    invalid handle instead of allocating.
 * 3. A 4-slot tick channel carries the frame index with the heap signal's
 *    reentrancy and lifecycle features, heap-free: a one-shot startup hook
 *    fires on the first tick and sweeps itself, an emit_blocker mutes one tick
 *    and restores emission on scope exit, and a watchdog disconnects the frame
 *    logger from inside its own invocation, a removal the signal defers to the
 *    end of the outermost emit so the running iteration stays valid.
 *
 * The same trade-offs as showcase.cpp's heap-free section, examined on their
 * own: pick static_signal when the slot count is known and the heap is unwanted.
 */

#include <print>

#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/static_signal.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace sg = nexenne::signal;

/// @brief A sensor that owns a 4-slot, heap-free signal and exposes a connect-only sink.
class sensor {
public:
  [[nodiscard]] auto readings() noexcept -> sg::static_sink<void(double), 4> {
    return m_on_reading.as_sink();
  }

  auto publish(double const celsius) noexcept -> void {
    m_on_reading.emit(celsius);
  }

private:
  sg::static_signal<void(double), 4> m_on_reading{};  ///< Up to 4 slots, zero heap.
};

/// @brief A subscriber whose static_slot tracker ties its subscription to its lifetime.
class display {
public:
  explicit display(sg::static_sink<void(double), 4> source) noexcept {
    source.connect<&display::show>(*this, m_tracker);
  }

  auto show(double const celsius) noexcept -> void {
    std::println("  display: {:.1f} C", celsius);
  }

private:
  sg::static_slot<2> m_tracker{};  ///< Disconnects its tracked subscriptions on destruction.
};

}  // namespace

auto main() -> int {
  auto temp{sensor{}};

  [[maybe_unused]] auto log{temp.readings().connect(
    [](double const c) noexcept { std::println("  log[prio -1]: {:.1f} C", c); }, -1
  )};

  {
    auto screen{display{temp.readings()}};
    std::println("two subscribers:");
    temp.publish(21.0);
  }

  std::println("after the display expired:");
  temp.publish(22.5);

  auto bus{sg::static_signal<void(), 2>{}};
  auto a{bus.connect([] noexcept {})};
  auto b{bus.connect([] noexcept {})};
  auto c{bus.connect([] noexcept {})};
  std::println(
    "\nstatic_signal<void(),2>: connected a={} b={}, third c={} (full, rejected)",
    a.has_target(),
    b.has_target(),
    c.has_target()
  );

  auto tick{sg::static_signal<void(int), 4>{}};

  [[maybe_unused]] auto const startup{tick.connect_once([](int frame) noexcept {
    std::println("  startup on frame {}", frame);
  })};

  auto logger{tick.connect([](int frame) noexcept { std::println("  frame {}", frame); })};
  [[maybe_unused]] auto const watchdog{tick.connect([&logger](int frame) noexcept {
    if (frame == 2) {
      std::println("  watchdog silences the logger");
      nexenne::utility::ignore(logger.disconnect());
    }
  })};

  std::println("\nheap-free tick channel:");
  tick.emit(1);
  {
    auto const paused{sg::emit_blocker{tick}};
    std::println("paused (this tick is suppressed):");
    tick.emit(99);
  }
  tick.emit(2);
  std::println("after the watchdog fired (logger gone):");
  tick.emit(3);
}
