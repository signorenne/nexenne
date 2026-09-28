/**
 * @file
 * @brief A tiny ECS simulation: a movement system over a multi-component view
 *        with an exclude tag, plus a lifecycle signal and entity recycling.
 *
 * Spawns a handful of entities and walks them through a short run:
 *
 *   1. An on_destroy<position> listener reports every despawn, whether it comes
 *      from destroy or from remove.
 *   2. Five entities get position and velocity; entity 2 is tagged frozen.
 *   3. A movement system integrates the non-frozen ones for three steps through
 *      view<position, velocity>().exclude<frozen>(), driven by the smaller
 *      storage.
 *   4. Destroying an entity fires the listener, drops every component it holds
 *      and frees its index for recycling.
 */

#include <print>
#include <vector>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/utility/ignore.hpp>

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

struct frozen {};

}  // namespace

auto main() -> int {
  auto reg{ec::registry{}};

  [[maybe_unused]] auto despawn{
    reg.on_destroy<position>().connect([](ec::entity_id const e, position const& p) noexcept {
      std::println("  despawn entity {} at ({:.1f}, {:.1f})", e.index(), p.x, p.y);
    })
  };

  auto ents{std::vector<ec::entity_id>{}};
  for (auto i{0}; i < 5; ++i) {
    auto const e{reg.create()};
    reg.add<position>(e, {static_cast<float>(i), 0.0f});
    reg.add<velocity>(e, {1.0f, static_cast<float>(i)});
    ents.push_back(e);
  }
  reg.add<frozen>(ents[2], {});

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

  std::println("destroying entity {}:", ents[0].index());
  reg.destroy(ents[0]);
  std::println("alive entities: {}", reg.alive());
}
