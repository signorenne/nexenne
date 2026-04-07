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
 * exists. This file demonstrates two such reactions and the exact firing order.
 *
 * Connections own the subscription: keep the returned connection alive for as
 * long as you want the callback to run, and drop it to unsubscribe. Read it top
 * to bottom.
 */

#include <print>
#include <vector>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/utility/discard.hpp>

namespace ecs = nexenne::ecs;

namespace {

struct score {
  int points{};
};

}  // namespace

auto main() -> int {
  auto reg{ecs::registry{}};

  // 1. Wire all three signals for `score` and watch the firing order. We track
  // a running total that stays correct purely by reacting: +points on construct,
  // the delta on update, and -points on destroy. No system ever recomputes it.
  std::println("== 1. Construct / update / destroy order ==");
  int total{0};

  auto on_add{
    reg.on_construct<score>().connect([&total](ecs::entity_id const e, score const& s) noexcept {
      total += s.points;
      std::println("  construct: entity {} +{}  (total {})", e.index(), s.points, total);
    })
  };

  // on_update sees the value AFTER the change. We do not know the old value here,
  // so this listener just reports; the patch below recomputes the total directly.
  auto on_change{
    reg.on_update<score>().connect([](ecs::entity_id const e, score const& s) noexcept {
      std::println("  update:    entity {} now {}", e.index(), s.points);
    })
  };

  auto on_remove{
    reg.on_destroy<score>().connect([&total](ecs::entity_id const e, score const& s) noexcept {
      total -= s.points;
      std::println("  destroy:   entity {} -{}  (total {})", e.index(), s.points, total);
    })
  };
  nexenne::utility::discard(on_add);
  nexenne::utility::discard(on_change);
  nexenne::utility::discard(on_remove);

  auto const a{reg.create()};
  auto const b{reg.create()};
  reg.add<score>(a, {10});  // fires on_construct
  reg.add<score>(b, {25});  // fires on_construct

  // add over an existing component fires on_update, not on_construct. We adjust
  // the running total ourselves around the replacement (the signal only reports).
  total += 5;               // 10 -> 15
  reg.add<score>(a, {15});  // fires on_update

  // patch mutates in place and also fires on_update.
  reg.patch<score>(b, [&total](score& s) noexcept {
    total += 5;  // 25 -> 30
    s.points += 5;
  });
  std::println("  total after edits: {}", total);

  // 2. on_destroy fires for remove<T> and for destroy(entity) alike. The
  // listener sees the live value one last time, so it can clean up correctly.
  std::println("== 2. on_destroy via remove and via destroy ==");
  reg.remove<score>(a);  // fires on_destroy for a's score
  reg.destroy(b);        // fires on_destroy for b's score, then frees b
  std::println("  final total: {} (back to zero)", total);

  // 3. An observer index built entirely from signals. on_construct appends to a
  // recently-spawned log and on_destroy could prune it; here we just collect the
  // ids of every entity that ever gained a score. This is the pattern behind
  // reactive systems, spatial indices, and dirty-tracking: the registry pushes
  // changes to you, so you never scan the whole world to find what moved.
  std::println("== 3. A signal-built observer log ==");
  auto spawned_log{std::vector<ecs::entity_id>{}};
  auto observer{reg.on_construct<score>().connect(
    [&spawned_log](ecs::entity_id const e, score const&) noexcept { spawned_log.push_back(e); }
  )};
  nexenne::utility::discard(observer);

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
