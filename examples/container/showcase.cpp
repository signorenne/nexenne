/**
 * @file
 * @brief A guided tour of nexenne::container through one realistic task: the
 *        core data model and a few ticks of a tiny, console-only game world.
 *
 * This program does not render or simulate physics - it *manages state* the way
 * a real entity system does, and prints what happens, so you can see how the
 * containers of the module fit together in context. The job each one is doing is
 * the point: every container here is the data-structure answer to one concrete
 * access pattern, and the comments say which pattern, and what it beats.
 *
 *   1. The entity store     -> slot_map: stable handles that survive recycling.
 *   2. The name index       -> flat_hash_map: name -> handle, one cache miss.
 *   3. Per-entity components -> small_vector: usually tiny, no heap traffic.
 *   4. The tick scheduler    -> indexed_priority_queue: pop-soonest + reschedule.
 *   5. The event log         -> ring_buffer: a fixed rolling window, never grows.
 *   6. Squad connectivity    -> union_find: who is linked to whom, near-O(1).
 *
 * Read it top to bottom; each section notes *why* its container is the right
 * tool and which simpler choice it improves on.
 */

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

namespace cn = nexenne::container;

namespace {

// A component tag attached to an entity. Most entities carry only a handful, so
// the per-entity list lives inline (see entity::components below).
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

// One game object. The component list is a small_vector<_, 4>: a real entity
// almost always has four or fewer components, so the list sits in the inline
// buffer and an entity costs zero extra allocations. It only spills to the heap
// for the rare heavily-decorated boss, and transparently so.
struct entity {
  std::string name;
  int health{};
  cn::small_vector<component, 4> components;
};

}  // namespace

auto main() -> int {
  // 1. The entity store: slot_map.
  //
  // Entities are created and destroyed constantly, and everything else (the AI,
  // the scheduler, the spatial index) needs to *refer* to an entity that may
  // already be gone. A raw pointer or a vector index would dangle or silently
  // alias a recycled slot. slot_map hands out an opaque key (slot + generation);
  // a key to a dead entity reads as absent, never as the new occupant of its
  // recycled slot. That ABA-safety is exactly what an entity handle needs.
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
  // A boss with five components: its list outgrows the inline-4 buffer and spills
  // to the heap, with no change to the calling code.
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

  // 2. The name index: flat_hash_map.
  //
  // The console and scripts look entities up by name, but the store is keyed by
  // handle. We keep a side index name -> handle. flat_hash_map stores its slots
  // in one contiguous array with linear probing, so a lookup is ~one cache miss
  // (and the whole map is one allocation, not a node per entry), several times
  // faster than std::unordered_map's node-per-entry layout. The handle is a tiny
  // trivially-copyable value, so the map owns copies and stays valid even as the
  // entity store reallocates.
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

  // 3. The tick scheduler: indexed_priority_queue.
  //
  // Each entity wants to "think" at some future tick; we always want the soonest
  // one. A min-heap gives pop-soonest in O(log n). But entities also reschedule
  // (an AI that just acted sleeps longer) and cancel (a dead entity stops
  // thinking) - operations std::priority_queue cannot do without a full rescan.
  // indexed_priority_queue returns a stable handle from push, so update() and
  // erase() are O(log n) by identity. std::greater makes it a min-heap on time.
  std::println("== 3. Tick scheduler (indexed_priority_queue) ==");
  cn::indexed_priority_queue<int, std::greater<int>> scheduler;  // min-heap on next-think tick
  cn::flat_hash_map<std::string, std::uint32_t> think_handle;    // entity name -> scheduler handle
  for (auto const& [who, first_tick] :
       {std::pair{std::string_view{"hero"}, 2},
        {std::string_view{"goblin"}, 1},
        {std::string_view{"boss"}, 5}}) {
    think_handle.insert_or_assign(std::string{who}, scheduler.push(first_tick));
  }

  // The goblin acts early, then reschedules itself far out: update() re-heapifies
  // in place, no scan to find its old entry.
  if (auto const* const h{think_handle.find("goblin")}) {
    static_cast<void>(scheduler.update(*h, 9));
  }
  std::println("  soonest think now at tick {}", *scheduler.top());  // hero at 2

  // 4. The event log: ring_buffer.
  //
  // A debug overlay shows the last few things that happened. We never want this
  // to grow without bound, and we never want it to allocate mid-frame. A
  // fixed-capacity ring_buffer keeps exactly the last N events: push_overwrite
  // drops the oldest once full and cannot fail, so logging is a single branchless
  // write into a circular array - the right shape for a hot per-frame logger.
  std::println("== 4. Event log (ring_buffer) ==");
  cn::ring_buffer<std::string, 4> events;  // only the last 4 events survive
  auto log = [&events](std::string msg) { events.push_overwrite(std::move(msg)); };

  // 5. Squad connectivity: union_find.
  //
  // The AI groups entities into squads: linking two entities should put their
  // whole squads in one set, and "are these two on the same side?" must be cheap.
  // union_find is the disjoint-set answer: unite() merges two sets and connected()
  // answers membership, both in near-constant amortised time (inverse Ackermann),
  // far cheaper than re-running a flood fill over an adjacency list every query.
  // We index it by slot, so an entity's slot index doubles as its squad node.
  std::println("== 5. Squad connectivity (union_find) ==");
  cn::union_find_u32 squads{static_cast<std::uint32_t>(world.capacity())};
  // The hero and the boss ally; the goblin and the crate are incidental.
  static_cast<void>(squads.unite(hero.index(), boss.index()));
  std::println("  hero & boss same squad: {}", *squads.connected(hero.index(), boss.index()));
  std::println("  hero & goblin same squad: {}", *squads.connected(hero.index(), goblin.index()));

  // 6. Run a few ticks: the containers working together.
  //
  // Each tick pops the soonest thinker, resolves it through the name index back
  // to its live entity in the store, mutates the world, logs to the ring buffer,
  // and reschedules. A dead entity's scheduler entry is erased by handle, and its
  // slot_map key quietly stops resolving - no dangling references anywhere.
  std::println("== 6. Simulation ==");
  // Reverse the scheduler handle -> name so a popped tick can name its owner.
  cn::flat_hash_map<std::uint32_t, std::string> owner_of;
  for (auto const& [who, h] : think_handle) {
    owner_of.insert_or_assign(h, who);
  }

  for (int step{0}; step < 5 && !scheduler.empty(); ++step) {
    // Peek the soonest thinker - we reschedule it in place with update() rather
    // than pop(), so its handle stays valid. (pop() would free the handle, and a
    // later update() on it would be a no-op.) erase() removes one when it should
    // stop thinking.
    auto const top_h{*scheduler.top_handle()};
    auto const tick{*scheduler.top()};
    auto const* const who{owner_of.find(top_h)};
    if (who == nullptr) {
      static_cast<void>(scheduler.erase(top_h));  // unknown handle (cannot happen here)
      continue;
    }
    auto* const actor{resolve(*who)};
    if (actor == nullptr) {
      static_cast<void>(scheduler.erase(top_h));  // entity died earlier; cancel its stale think
      continue;
    }

    if (*who == "hero") {
      if (auto* const target{resolve("goblin")}) {
        // The hero strikes the goblin.
        target->health -= 25;
        log(std::format(
          "t{}: hero hits goblin ({} hp left)", tick, target->health < 0 ? 0 : target->health
        ));
        if (target->health <= 0) {
          // The goblin dies: erase it from the store and cancel its scheduled
          // think by handle. Its name lookup will now miss; its slot may recycle.
          if (auto const* const gh{by_name.find("goblin")}) {
            static_cast<void>(world.erase(*gh));
          }
          if (auto const* const gth{think_handle.find("goblin")}) {
            static_cast<void>(scheduler.erase(*gth));
          }
          by_name.erase("goblin");
          log(std::format("t{}: goblin defeated", tick));
        }
      } else {
        log(std::format("t{}: hero patrols (no target)", tick));
      }
      // The hero thinks again two ticks later: update() re-heapifies by handle.
      static_cast<void>(scheduler.update(top_h, tick + 2));
    } else {
      log(std::format("t{}: {} thinks (hp {})", tick, *who, actor->health));
      static_cast<void>(scheduler.update(top_h, tick + 3));  // reschedule, three ticks out
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
