/**
 * @file
 * @brief A focused tour of views and queries: single vs multi-component
 *        iteration, exclude filters, the fluent query builder, and range-for.
 *
 * Views are the read/write half of an ECS: a system declares which components
 * it touches and the view hands it exactly those, for exactly the entities that
 * carry them. This file builds one small population (4 movers with position and
 * velocity, one of them tagged sleeping, plus 2 position-only statics) and looks
 * at it five ways:
 *
 *   1. Single-component view  -> the densest, fastest pass; it also visits the
 *      statics, and the entity-id callback form is picked when the lambda
 *      takes one.
 *   2. Multi-component view   -> the intersection of two storages, one
 *      reference per include: write position, read velocity.
 *   3. Exclude filter         -> set difference via .exclude<>(), one O(1)
 *      membership test per candidate.
 *   4. Fluent query builder   -> .with<> / .without<> / .each<>; the filter
 *      set is part of the type, so a typo does not compile.
 *   5. Range-for over a view  -> structured bindings, same match set.
 *
 * The view always drives off the SMALLEST include storage, so a view over a
 * rare component and a common one walks the rare one and probes the common one,
 * never the other way around. Read it top to bottom.
 */

#include <print>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/utility/ignore.hpp>

namespace ecs = nexenne::ecs;

namespace {

struct position {
  float x{};
  float y{};
};

struct velocity {
  float x{};
  float y{};
};

struct sleeping {};

}  // namespace

auto main() -> int {
  auto reg{ecs::registry{}};

  std::println("== Population ==");
  for (int i{0}; i < 4; ++i) {
    auto const e{reg.create()};
    reg.add<position>(e, {static_cast<float>(i), 0.0F});
    reg.add<velocity>(e, {1.0F, static_cast<float>(i)});
    if (i == 1) {
      reg.add<sleeping>(e, {});
    }
  }
  for (int i{0}; i < 2; ++i) {
    auto const e{reg.create()};
    reg.add<position>(e, {10.0F + static_cast<float>(i), 0.0F});
  }
  std::println("  6 entities: 4 have velocity (1 asleep), 2 are position-only");

  std::println("== 1. view<position> (everyone with a position) ==");
  int with_position{0};
  reg.view<position>().each([&with_position](ecs::entity_id const e, position const& p) noexcept {
    ++with_position;
    std::println("  entity {} at ({:.1f}, {:.1f})", e.index(), p.x, p.y);
  });
  std::println("  total with position: {}", with_position);

  std::println("== 2. view<position, velocity> (movers only) ==");
  int movers{0};
  reg.view<position, velocity>().each([&movers](position& p, velocity const& v) noexcept {
    p.x += v.x;
    p.y += v.y;
    ++movers;
  });
  std::println("  integrated {} movers (statics untouched)", movers);

  std::println("== 3. view<position, velocity>.exclude<sleeping>() ==");
  int awake{0};
  reg.view<position, velocity>().exclude<sleeping>().each([&awake](position&, velocity&) noexcept {
    ++awake;
  });
  std::println("  awake movers: {} (the sleeper is filtered out)", awake);

  std::println("== 4. query().with<position>().with<velocity>().without<sleeping>() ==");
  int queried{0};
  reg.query().with<position>().with<velocity>().without<sleeping>().each(
    [&queried](position const& p, velocity const&) noexcept {
      ++queried;
      std::println("  awake mover now at x={:.1f}", p.x);
    }
  );
  std::println("  query matched: {}", queried);

  std::println("== 5. range-for over view<position, velocity> ==");
  float sum_x{0.0F};
  for ([[maybe_unused]] auto [e, p, v] : reg.view<position, velocity>()) {
    sum_x += p.x;
  }
  std::println("  sum of mover x-coords: {:.1f}", sum_x);

  std::println("\nA view is a system's contract: name the components you touch,");
  std::println("and the registry hands you exactly the matching entities, dense");
  std::println("and intersected, with includes required and excludes forbidden.");
  return 0;
}
