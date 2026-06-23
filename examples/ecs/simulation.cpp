/**
 * @file
 * @brief A tiny ECS simulation: a movement system over a multi-component view
 *        with an exclude tag, plus a lifecycle signal and entity recycling.
 *
 * Spawns a handful of entities with position and velocity, freezes one with a
 * tag component, integrates the non-frozen ones each step through a
 * view<position, velocity>().exclude<frozen>(), watches despawns via the
 * on_destroy<position> signal, and destroys one entity to show that every
 * component goes with it.
 */

#include <print>
#include <vector>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace ec = nexenne::ecs;

struct position {
  float x{};
  float y{};
};

struct velocity {
  float x{};
  float y{};
};

struct frozen {};  // tag component: excluded from the movement system

}  // namespace

auto main() -> int {
  auto reg{ec::registry{}};

  // React to a position being torn down (on destroy or remove).
  auto despawn{
    reg.on_destroy<position>().connect([](ec::entity_id const e, position const& p) noexcept {
      std::println("  despawn entity {} at ({:.1f}, {:.1f})", e.index(), p.x, p.y);
    })
  };
  nexenne::utility::discard(despawn);

  // Spawn five entities; entity 2 is frozen so the movement system skips it.
  auto ents{std::vector<ec::entity_id>{}};
  for (auto i{0}; i < 5; ++i) {
    auto const e{reg.create()};
    reg.add<position>(e, {static_cast<float>(i), 0.0f});
    reg.add<velocity>(e, {1.0f, static_cast<float>(i)});
    ents.push_back(e);
  }
  reg.add<frozen>(ents[2], {});

  // Movement system: integrate position by velocity for every non-frozen
  // entity, three steps. The view drives off the smaller of the two storages.
  for (auto step{0}; step < 3; ++step) {
    reg.view<position, velocity>().exclude<frozen>().each(
      [](position& p, velocity const& v) noexcept {
        p.x += v.x;
        p.y += v.y;
      }
    );
  }

  std::println("positions after 3 steps (entity 2 frozen, so unmoved):");
  for (auto const e : reg) {
    if (auto const p{reg.get<position>(e)}; p.has_value()) {
      auto const& pos{p.value().get()};
      std::println("  entity {}: ({:.1f}, {:.1f})", e.index(), pos.x, pos.y);
    }
  }

  // Destroying an entity fires on_destroy<position>, drops all its components,
  // and frees its index for recycling.
  std::println("destroying entity {}:", ents[0].index());
  reg.destroy(ents[0]);
  std::println("alive entities: {}", reg.alive());
}
