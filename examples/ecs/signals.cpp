/**
 * @file
 * @brief A focused tour of the per-component lifecycle signals:
 *        on_construct, on_update, on_destroy, and what they let you build.
 *
 * The registry fires three signals per component type as components come and go:
 *
 *   - on_construct<T>  after a NEW T is attached (add of a fresh component).
 *   - on_update<T>     after an existing T is replaced (add over an existing
 *                      one) or mutated in place (patch).
 *   - on_destroy<T>    just BEFORE a T is removed (remove<T> or destroy(e)), so
 *                      the listener still sees the final value.
 *
 * Reacting to these instead of polling is what keeps systems decoupled: an index
 * or a counter can stay in sync with the world without any system knowing it
 * exists. This file demonstrates two such reactions and the exact firing order:
 *
 *   1. A running score total kept by reacting: +points on construct, -points on
 *      destroy. on_update sees only the value after the change, not the old
 *      one, so it just reports and the replacement and the patch adjust the
 *      total themselves.
 *   2. on_destroy fires for remove<T> and for destroy(e) alike, handing the
 *      listener the live value one last time.
 *   3. An observer log built from on_construct alone: the pattern behind
 *      reactive systems, spatial indices and dirty-tracking, where the registry
 *      pushes changes instead of a system rescanning the world.
 *
 * Connections own the subscription: keep the returned connection alive for as
 * long as you want the callback to run, and drop it to unsubscribe. Read it top
 * to bottom.
 */

#include <print>
#include <vector>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/utility/ignore.hpp>

namespace ecs = nexenne::ecs;

namespace {

struct score {
  int points{};
};

}  // namespace

auto main() -> int {
  auto reg{ecs::registry{}};

  std::println("== 1. Construct / update / destroy order ==");
  int total{0};

  [[maybe_unused]] auto on_add{
    reg.on_construct<score>().connect([&total](ecs::entity_id const e, score const& s) noexcept {
      total += s.points;
      std::println("  construct: entity {} +{}  (total {})", e.index(), s.points, total);
    })
  };

  [[maybe_unused]] auto on_change{
    reg.on_update<score>().connect([](ecs::entity_id const e, score const& s) noexcept {
      std::println("  update:    entity {} now {}", e.index(), s.points);
    })
  };

  [[maybe_unused]] auto on_remove{
    reg.on_destroy<score>().connect([&total](ecs::entity_id const e, score const& s) noexcept {
      total -= s.points;
      std::println("  destroy:   entity {} -{}  (total {})", e.index(), s.points, total);
    })
  };

  auto const a{reg.create()};
  auto const b{reg.create()};
  reg.add<score>(a, {10});
  reg.add<score>(b, {25});

  total += 5;
  reg.add<score>(a, {15});

  reg.patch<score>(b, [&total](score& s) noexcept {
    total += 5;
    s.points += 5;
  });
  std::println("  total after edits: {}", total);

  std::println("== 2. on_destroy via remove and via destroy ==");
  reg.remove<score>(a);
  reg.destroy(b);
  std::println("  final total: {} (back to zero)", total);

  std::println("== 3. A signal-built observer log ==");
  auto spawned_log{std::vector<ecs::entity_id>{}};
  [[maybe_unused]] auto observer{reg.on_construct<score>().connect(
    [&spawned_log](ecs::entity_id const e, score const&) noexcept { spawned_log.push_back(e); }
  )};

  for (int i{0}; i < 3; ++i) {
    auto const e{reg.create()};
    reg.add<score>(e, {i});
  }
  std::println("  observer recorded {} new score-holders", spawned_log.size());
  for (auto const e : spawned_log) {
    std::println("    logged entity {}", e.index());
  }

  std::println("\nSignals let bookkeeping ride along with the data: a total, an");
  std::println("index, or a log stays correct by reacting to construct / update /");
  std::println("destroy, so no system has to rescan the world to stay in sync.");
  return 0;
}
