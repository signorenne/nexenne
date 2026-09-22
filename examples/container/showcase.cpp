/**
 * @file
 * @brief A guided tour of nexenne::container through one realistic task: the
 *        core data model and a few ticks of a tiny, console-only game world.
 *
 * This program does not render or simulate physics; it manages state the way a
 * real entity system does, and prints what happens, so you can see how the
 * containers of the module fit together in context. Every container here is the
 * data-structure answer to one concrete access pattern, and each printed section
 * matches one step below, which says why its container is the right tool and
 * which simpler choice it improves on.
 *
 * 1. The entity store, slot_map: entities come and go, and everything else needs
 *    to refer to one that may already be gone. A raw pointer or a vector index
 *    would dangle or silently alias a recycled slot; a slot_map key (slot plus
 *    generation) to a dead entity reads as absent, never as the new occupant.
 *    Each entity's component list is a small_vector of four: almost every entity
 *    fits inline at zero extra allocations, and only the five-component boss
 *    spills to the heap, with no change to the calling code.
 * 2. The name index, flat_hash_map: name to handle in about one cache miss, the
 *    slots living in one contiguous linear-probing array (one allocation, not a
 *    node per entry), several times faster than std::unordered_map. The handle is
 *    a tiny trivially copyable value, so the index stays valid as the store
 *    reallocates.
 * 3. The tick scheduler, indexed_priority_queue: always the soonest thinker,
 *    but entities also reschedule and cancel, which std::priority_queue cannot do
 *    without a full rescan. The stable handle from push makes update and erase
 *    O(log n) by identity; std::greater makes it a min-heap on time.
 * 4. The event log, ring_buffer: exactly the last few events, never growing and
 *    never allocating mid-frame. push_overwrite drops the oldest once full and
 *    cannot fail, a single write into a circular array.
 * 5. Squad connectivity, union_find: unite merges two squads and connected
 *    answers "same side?" in near-constant amortised time (inverse Ackermann),
 *    far cheaper than a flood fill per query. An entity's slot index doubles as
 *    its squad node.
 * 6. The simulation: each tick peeks the soonest thinker, resolves it through the
 *    name index to its live entity, mutates the world, logs, and reschedules in
 *    place with update, since pop would free the handle. A dead entity's think is
 *    erased by handle and its key quietly stops resolving: no dangling reference
 *    anywhere.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <print>
#include <string>
#include <string_view>

#include <nexenne/container/flat_hash_map.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>
#include <nexenne/container/ring_buffer.hpp>
#include <nexenne/container/slot_map.hpp>
#include <nexenne/container/small_vector.hpp>
#include <nexenne/container/union_find.hpp>
#include <nexenne/utility/ignore.hpp>

namespace cn = nexenne::container;

namespace {

enum class component : std::uint8_t {
  transform,
  health,
  ai,
  inventory,
  collider,
};

constexpr auto name_of(component const c) noexcept -> std::string_view {
  switch (c) {
    case component::transform:
      return "transform";
    case component::health:
      return "health";
    case component::ai:
      return "ai";
    case component::inventory:
      return "inventory";
    case component::collider:
      return "collider";
  }
  return "?";
}

struct entity {
  std::string name;
  int health{};
  cn::small_vector<component, 4> components;
};

}  // namespace

auto main() -> int {
  std::println("== 1. Entity store (slot_map) ==");
  cn::slot_map<entity> world;
  using handle = cn::slot_map<entity>::key;

  auto spawn = [&world](std::string n, int hp, auto... comps) -> handle {
    entity e{.name = std::move(n), .health = hp, .components = {}};
    (e.components.push_back(comps), ...);
    return world.insert(std::move(e));
  };

  auto const hero{
    spawn("hero", 100, component::transform, component::health, component::ai, component::inventory)
  };
  auto const goblin{
    spawn("goblin", 20, component::transform, component::health, component::ai, component::collider)
  };
  auto const crate{spawn("crate", 1, component::transform, component::collider)};
  auto const boss{spawn(
    "boss",
    500,
    component::transform,
    component::health,
    component::ai,
    component::inventory,
    component::collider
  )};

  std::println("  spawned {} entities", world.size());
  if (auto const* const e{world.find(boss)}) {
    std::println(
      "  boss has {} components, inline storage: {}",
      e->components.size(),
      e->components.is_inline()
    );
  }

  std::println("== 2. Name index (flat_hash_map) ==");
  cn::flat_hash_map<std::string, handle> by_name;
  for (auto const& h : {hero, goblin, crate, boss}) {
    if (auto const* const e{world.find(h)}) {
      by_name.insert_or_assign(e->name, h);
    }
  }

  auto resolve = [&](std::string_view who) -> entity* {
    auto const* const h{by_name.find(std::string{who})};
    return h == nullptr ? nullptr : world.find(*h);
  };

  if (auto const* const e{resolve("goblin")}) {
    std::print("  'goblin' -> hp {}, components:", e->health);
    for (component const c : e->components) {
      std::print(" {}", name_of(c));
    }
    std::println("");
  }
  std::println(
    "  name index holds {} entries (load factor {:.2f})", by_name.size(), by_name.load_factor()
  );

  std::println("== 3. Tick scheduler (indexed_priority_queue) ==");
  cn::indexed_priority_queue<int, std::greater<int>> scheduler;
  cn::flat_hash_map<std::string, std::uint32_t> think_handle;
  for (auto const& [who, first_tick] :
       {std::pair{std::string_view{"hero"}, 2},
        {std::string_view{"goblin"}, 1},
        {std::string_view{"boss"}, 5}}) {
    think_handle.insert_or_assign(std::string{who}, scheduler.push(first_tick));
  }

  if (auto const* const h{think_handle.find("goblin")}) {
    nexenne::utility::ignore(scheduler.update(*h, 9));
  }
  std::println("  soonest think now at tick {}", *scheduler.top());

  std::println("== 4. Event log (ring_buffer) ==");
  cn::ring_buffer<std::string, 4> events;
  auto log = [&events](std::string msg) { events.push_overwrite(std::move(msg)); };

  std::println("== 5. Squad connectivity (union_find) ==");
  cn::union_find_u32 squads;
  for (std::size_t slot{0}; slot < world.capacity(); ++slot) {
    if (!squads.make_set()) {
      return 1;
    }
  }
  nexenne::utility::ignore(squads.unite(hero.index(), boss.index()));
  std::println("  hero & boss same squad: {}", *squads.connected(hero.index(), boss.index()));
  std::println("  hero & goblin same squad: {}", *squads.connected(hero.index(), goblin.index()));

  std::println("== 6. Simulation ==");
  cn::flat_hash_map<std::uint32_t, std::string> owner_of;
  for (auto const& [who, h] : think_handle) {
    owner_of.insert_or_assign(h, who);
  }

  for (int step{0}; step < 5 && !scheduler.empty(); ++step) {
    auto const top_h{*scheduler.top_handle()};
    auto const tick{*scheduler.top()};
    auto const* const who{owner_of.find(top_h)};
    if (who == nullptr) {
      nexenne::utility::ignore(scheduler.erase(top_h));
      continue;
    }
    auto* const actor{resolve(*who)};
    if (actor == nullptr) {
      nexenne::utility::ignore(scheduler.erase(top_h));
      continue;
    }

    if (*who == "hero") {
      if (auto* const target{resolve("goblin")}) {
        target->health -= 25;
        log(
          std::format(
            "t{}: hero hits goblin ({} hp left)", tick, target->health < 0 ? 0 : target->health
          )
        );
        if (target->health <= 0) {
          if (auto const* const gh{by_name.find("goblin")}) {
            nexenne::utility::ignore(world.erase(*gh));
          }
          if (auto const* const gth{think_handle.find("goblin")}) {
            nexenne::utility::ignore(scheduler.erase(*gth));
          }
          by_name.erase("goblin");
          log(std::format("t{}: goblin defeated", tick));
        }
      } else {
        log(std::format("t{}: hero patrols (no target)", tick));
      }
      nexenne::utility::ignore(scheduler.update(top_h, tick + 2));
    } else {
      log(std::format("t{}: {} thinks (hp {})", tick, *who, actor->health));
      nexenne::utility::ignore(scheduler.update(top_h, tick + 3));
    }
  }

  std::println("  live entities after simulation: {}", world.size());
  std::println("  'goblin' resolves now: {}", resolve("goblin") != nullptr);
  std::print("  recent events (oldest first, last {} kept):\n", events.size());
  for (auto const& msg : events) {
    std::println("    {}", msg);
  }

  std::println("\nThat is the whole module in one tick loop: handle-stable storage,");
  std::println("a flat hash index, inline component lists, a re-orderable scheduler,");
  std::println("a fixed event window, and disjoint-set connectivity - each the answer");
  std::println("to one access pattern the simpler choice could not serve.");
  return 0;
}
