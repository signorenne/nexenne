/**
 * @file
 * @brief Heap-free pub/sub with nexenne::signal::static_signal.
 *
 * A fixed-capacity signal that allocates nothing: the slot storage lives inline
 * in the signal. Shows capacity handling (a connect past the bound fails rather
 * than allocating), priority ordering, lifetime tracking with static_slot, and
 * a connect-only sink.
 */

#include <print>

#include <nexenne/signal/static_signal.hpp>

namespace {

namespace sg = nexenne::signal;

// A sensor that owns a 4-slot, heap-free signal and exposes a connect-only sink.
class sensor {
public:
  [[nodiscard]] auto readings() noexcept -> sg::static_sink<void(double), 4> {
    return m_on_reading.as_sink();
  }

  auto publish(double const celsius) noexcept -> void {
    m_on_reading.emit(celsius);
  }

private:
  sg::static_signal<void(double), 4> m_on_reading{};  // up to 4 slots, zero heap
};

// A subscriber that ties its subscription to its own lifetime via a tracker.
class display {
public:
  explicit display(sg::static_sink<void(double), 4> source) noexcept {
    source.connect<&display::show>(*this, m_tracker);
  }

  auto show(double const celsius) noexcept -> void {
    std::println("  display: {:.1f} C", celsius);
  }

private:
  sg::static_slot<2> m_tracker{};  // auto-disconnects on destruction
};

}  // namespace

auto main() -> int {
  auto temp{sensor{}};

  // A priority-ordered logger (lower priority fires first).
  auto log{temp.readings().connect(
    [](double const c) noexcept { std::println("  log[prio -1]: {:.1f} C", c); }, -1
  )};

  {
    auto screen{display{temp.readings()}};
    std::println("two subscribers:");
    temp.publish(21.0);  // logger (prio -1) then display
  }  // screen dies: its tracker disconnects the display subscription

  std::println("after the display expired:");
  temp.publish(22.5);  // only the logger remains

  static_cast<void>(log);

  // Capacity is fixed: a connect past the bound fails instead of allocating.
  auto bus{sg::static_signal<void(), 2>{}};
  auto a{bus.connect([] noexcept {})};
  auto b{bus.connect([] noexcept {})};
  auto c{bus.connect([] noexcept {})};  // bus is full
  std::println(
    "\nstatic_signal<void(),2>: connected a={} b={}, third c={} (full, rejected)",
    a.has_target(),
    b.has_target(),
    c.has_target()
  );
}
