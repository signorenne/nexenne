/**
 * @file
 * @brief A guided tour of nexenne::ecs through one realistic task: a tiny
 *        2D asteroids-style world, ticked forward a few frames.
 *
 * This program runs a small game-style simulation entirely on the console. It
 * spawns ships and asteroids, attaches several small component types to each,
 * and drives the world with a handful of systems that each read and write only
 * the components they care about:
 *
 *   1. Spawn      -> create() entities, add<> position / velocity / health /...
 *   2. Movement   -> view<position, velocity>(), excluding frozen things.
 *   3. Lifetime   -> patch<> a countdown, despawn at zero via on_destroy signal.
 *   4. Collision  -> a view pass that subtracts health on overlap.
 *   5. Reaping    -> destroy() entities whose health hit zero.
 *   6. Reporting  -> queries that count and inspect the survivors.
 *
 * The running theme is data-oriented composition. In an OOP object graph you
 * would write a GameObject base class, derive Ship and Asteroid, and bury the
 * loop inside virtual update() methods, so every kind of update touches the
 * full fat object and chases pointers across the heap. Here an entity is just
 * an id, behaviour lives in free functions ("systems"), and each system walks a
 * dense view of exactly the components it needs. Adding a new behaviour is a new
 * component plus a new system, with no base-class surgery and no recompiling the
 * things that do not care. Read it top to bottom.
 */

#include <array>
#include <cstddef>
#include <print>
#include <vector>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/utility/discard.hpp>

namespace ecs = nexenne::ecs;

namespace {

// Components are plain data: no methods, no inheritance, no vtables. Each is
// stored in its own dense pool, so a system that only needs position pays for
// position alone and never drags an asteroid's health through the cache.
struct position {
  float x{};
  float y{};
};

struct velocity {
  float x{};
  float y{};
};

struct health {
  int hp{};
};

struct radius {
  float r{};
};

// A bullet carries a countdown; when it reaches zero the bullet despawns.
struct lifetime {
  int ticks_left{};
};

// Tag components are empty: they carry no data, only presence. Used as cheap
// per-entity flags that views can require or exclude. std::is_empty_v is true,
// so they cost a slot bit, not a payload.
struct frozen {};  // excluded from the movement system

struct asteroid {};  // marks the big rocks

struct ship {};  // marks the player-controlled craft

}  // namespace

auto main() -> int {
  auto reg{ecs::registry{}};

  // 1. Spawn. An entity is born with create(), then composed by adding the
  // components that define what it is. Two entities can share component types
  // without sharing a class: a ship and an asteroid both have position, but only
  // the ship is tagged ship, only the rock is tagged asteroid. Composition, not
  // a rigid inheritance tree, decides behaviour.
  std::println("== 1. Spawn ==");

  // The lone ship: it has health, a body radius, and starts drifting right.
  auto const player{reg.create()};
  reg.add<position>(player, {0.0F, 0.0F});
  reg.add<velocity>(player, {1.5F, 0.0F});
  reg.add<health>(player, {100});
  reg.add<radius>(player, {1.0F});
  reg.add<ship>(player, {});

  // Three asteroids on a collision course toward the origin.
  auto rocks{std::vector<ecs::entity_id>{}};
  constexpr std::array<position, 3> rock_start{
    position{6.0F, 0.0F},
    position{4.0F, 0.5F},
    position{8.0F, -0.5F},
  };
  constexpr std::array<velocity, 3> rock_drift{
    velocity{-2.0F, 0.0F},
    velocity{-1.0F, 0.0F},
    velocity{-3.0F, 0.0F},
  };
  for (std::size_t i{0}; i < rock_start.size(); ++i) {
    auto const e{reg.create()};
    reg.add<position>(e, rock_start[i]);
    reg.add<velocity>(e, rock_drift[i]);
    reg.add<health>(e, {30});
    reg.add<radius>(e, {1.5F});
    reg.add<asteroid>(e, {});
    rocks.push_back(e);
  }

  // One frozen debris chunk: it has position and velocity like the rest, but the
  // frozen tag holds it out of the movement system. Same components, different
  // behaviour, decided by a tag rather than a subclass override.
  auto const debris{reg.create()};
  reg.add<position>(debris, {2.0F, 3.0F});
  reg.add<velocity>(debris, {9.0F, 9.0F});  // would fly off, but it is frozen
  reg.add<frozen>(debris, {});

  std::println("  spawned 1 ship, {} asteroids, 1 frozen debris", rocks.size());
  std::println("  alive entities: {}", reg.alive());

  // A despawn log, wired once. on_destroy<health> fires just before a health
  // component is torn down, whether by remove<health> or by destroy(entity). A
  // system never has to poll for deaths: it reacts to the signal. The returned
  // connection is kept alive for the program's duration.
  auto death_log{
    reg.on_destroy<health>().connect([](ecs::entity_id const e, health const& h) noexcept {
      std::println("  [signal] entity {} died with hp {}", e.index(), h.hp);
    })
  };
  nexenne::utility::discard(death_log);

  // 2. Movement system. view<position, velocity>() visits exactly the entities
  // that carry both, and .exclude<frozen>() drops the held-in-place debris. The
  // view drives off the smaller of the two storages and walks it densely, so the
  // hot loop is a tight pass over packed data, not a pointer chase through a
  // heterogeneous object list. The callback mutates position and reads velocity.
  auto const movement_step{[&reg]() noexcept {
    reg.view<position, velocity>().exclude<frozen>().each(
      [](position& p, velocity const& v) noexcept {
        p.x += v.x;
        p.y += v.y;
      }
    );
  }};

  // 3. Collision and damage. For each ship-asteroid pair we test whether their
  // bodies overlap (distance < sum of radii) and, if so, subtract health from
  // both. Two views compose here: the outer walk ranges over ships and the inner
  // over asteroids, so each ship's data is fetched once and reused across the
  // inner pass. Because we mutate health (a third storage) and not the storages
  // being iterated, the pointer-stable pools keep every reference the views
  // handed us valid.
  auto const collision_step{[&reg]() noexcept {
    reg.view<position, radius, health, ship>().each(
      [&reg](position const& sp, radius const& sr, health& sh, ship&) noexcept {
        reg.view<position, radius, health, asteroid>().each(
          [&sp, &sr, &sh](position const& ap, radius const& ar, health& ah, asteroid&) noexcept {
            auto const dx{ap.x - sp.x};
            auto const dy{ap.y - sp.y};
            auto const reach{sr.r + ar.r};
            if (dx * dx + dy * dy < reach * reach) {
              sh.hp -= 10;  // the ship is dented
              ah.hp -= 30;  // the rock is shattered
            }
          }
        );
      }
    );
  }};

  // 4. Reaping. Anything whose health dropped to zero or below is destroyed.
  // We cannot destroy entities while iterating the live-entity set in place, so
  // we collect the doomed in one pass and destroy them in a second. destroy()
  // fires on_destroy for every component the entity holds, then frees its slot
  // for recycling, so all of an entity's parts leave together with no manual
  // teardown per component type.
  auto const reap_step{[&reg]() noexcept -> int {
    auto doomed{std::vector<ecs::entity_id>{}};
    reg.view<health>().each([&doomed](ecs::entity_id const e, health const& h) noexcept {
      if (h.hp <= 0) {
        doomed.push_back(e);
      }
    });
    for (auto const e : doomed) {
      reg.destroy(e);
    }
    return static_cast<int>(doomed.size());
  }};

  // 5. Run the world for several frames. Each tick is just a fixed sequence of
  // systems; the scheduler is a plain list of calls in dependency order
  // (move, then collide, then reap). No central update() dispatch, no virtual
  // calls: the order is explicit and easy to reason about.
  std::println("== 2-5. Simulation ==");
  constexpr int frames{6};
  for (int frame{0}; frame < frames; ++frame) {
    movement_step();
    collision_step();
    auto const reaped{reap_step()};
    std::println("  frame {}: alive {:2}  (reaped {})", frame, reg.alive(), reaped);
  }

  // 6. Reporting through queries. The fluent query() builder accumulates include
  // and exclude filters at compile time, then runs each() over the match set. We
  // count survivors by kind and read out the player's final state. Every filter
  // is a type, so a typo is a compile error, not a silent empty result.
  std::println("== 6. Survivors ==");

  int ships_left{0};
  reg.query().with<ship>().with<health>().each([&ships_left](ship&, health const& h) noexcept {
    ++ships_left;
    std::println("  ship still flying with hp {}", h.hp);
  });
  if (ships_left == 0) {
    std::println("  the ship did not make it");
  }

  int rocks_left{0};
  reg.query().with<asteroid>().each([&rocks_left](asteroid&) noexcept { ++rocks_left; });
  std::println("  asteroids remaining: {}", rocks_left);

  // The frozen debris never moved: query for it and confirm. with<frozen>() and
  // get<position> together prove the movement system skipped it.
  reg.query().with<position>().with<frozen>().each(
    [](ecs::entity_id const e, position const& p, frozen&) noexcept {
      std::println("  frozen debris {} still at ({:.1f}, {:.1f})", e.index(), p.x, p.y);
    }
  );

  // 7. Lifecycle: a bullet that despawns itself. This shows the add / patch /
  // destroy flow and generation-safe handles independent of the big sim. We fire
  // a bullet, tick its lifetime down with patch<lifetime>, and when it expires
  // destroy the entity; that bumps its slot generation, so the stale handle then
  // reads as invalid.
  std::println("== 7. Bullet lifecycle ==");
  auto const bullet{reg.create()};
  reg.add<position>(bullet, {0.0F, 0.0F});
  reg.add<lifetime>(bullet, {3});
  std::println("  fired bullet {} (gen {})", bullet.index(), bullet.generation());

  bool expired{false};
  for (int t{0}; t < 5 && !expired; ++t) {
    reg.patch<lifetime>(bullet, [](lifetime& l) noexcept { l.ticks_left -= 1; });
    auto const l{reg.get<lifetime>(bullet)};
    if (l && l->get().ticks_left <= 0) {
      reg.destroy(bullet);  // bumps the slot generation, invalidating the handle
      expired = true;
    }
  }
  std::println("  bullet handle valid after expiry? {}", reg.valid(bullet));

  std::println("\nThat is the whole module in one world: entities composed from");
  std::println("small components, systems that each walk a dense view of just the");
  std::println("data they touch, lifecycle signals, generation-safe handles, and");
  std::println("filtered queries - all with zero virtual dispatch.");
  return 0;
}
