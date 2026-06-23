/**
 * @file
 * @brief A focused tour of views and queries: single vs multi-component
 *        iteration, exclude filters, the fluent query builder, and range-for.
 *
 * Views are the read/write half of an ECS: a system declares which components
 * it touches and the view hands it exactly those, for exactly the entities that
 * carry them. This file builds one small population and looks at it five ways:
 *
 *   1. Single-component view  -> the densest, fastest pass.
 *   2. Multi-component view   -> the intersection of two storages.
 *   3. Exclude filter         -> set difference via .exclude<>().
 *   4. Fluent query builder   -> .with<> / .without<> / .each<>.
 *   5. Range-for over a view  -> structured bindings, same match set.
 *
 * The view always drives off the SMALLEST include storage, so a view over a
 * rare component and a common one walks the rare one and probes the common one,
 * never the other way around. Read it top to bottom.
 */

#include <print>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/utility/discard.hpp>

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

struct sleeping {};  // tag: excluded from the movement pass

}  // namespace

auto main() -> int {
  auto reg{ecs::registry{}};

  // Build a small population:
  //   - 4 movers: position + velocity
  //   - 1 of those movers is also sleeping (a tag) and should be skipped
  //   - 2 statics: position only (no velocity), so multi-component views skip them
  std::println("== Population ==");
  for (int i{0}; i < 4; ++i) {
    auto const e{reg.create()};
    reg.add<position>(e, {static_cast<float>(i), 0.0F});
    reg.add<velocity>(e, {1.0F, static_cast<float>(i)});
    if (i == 1) {
      reg.add<sleeping>(e, {});  // this mover is asleep
    }
  }
  for (int i{0}; i < 2; ++i) {
    auto const e{reg.create()};
    reg.add<position>(e, {10.0F + static_cast<float>(i), 0.0F});  // no velocity
  }
  std::println("  6 entities: 4 have velocity (1 asleep), 2 are position-only");

  // 1. Single-component view. The simplest and densest case: walk every entity
  // carrying position. This includes the velocity-less statics. The entity-id
  // overload of the callback is selected automatically when the lambda takes it.
  std::println("== 1. view<position> (everyone with a position) ==");
  int with_position{0};
  reg.view<position>().each([&with_position](ecs::entity_id const e, position const& p) noexcept {
    ++with_position;
    std::println("  entity {} at ({:.1f}, {:.1f})", e.index(), p.x, p.y);
  });
  std::println("  total with position: {}", with_position);

  // 2. Multi-component view. view<position, velocity> visits only the
  // intersection: the 4 movers, not the 2 statics. The callback gets one
  // reference per include type. Taking position by value-mutable and velocity by
  // const is the usual "write target, read source" split.
  std::println("== 2. view<position, velocity> (movers only) ==");
  int movers{0};
  reg.view<position, velocity>().each([&movers](position& p, velocity const& v) noexcept {
    p.x += v.x;  // one integration step, to show writes land
    p.y += v.y;
    ++movers;
  });
  std::println("  integrated {} movers (statics untouched)", movers);

  // 3. Exclude filter. Chain .exclude<sleeping>() to drop the one tagged mover.
  // This is a set difference computed by an O(1) membership test per candidate,
  // so the asleep entity is visited by the driver then rejected by the filter.
  std::println("== 3. view<position, velocity>.exclude<sleeping>() ==");
  int awake{0};
  reg.view<position, velocity>().exclude<sleeping>().each([&awake](position&, velocity&) noexcept {
    ++awake;
  });
  std::println("  awake movers: {} (the sleeper is filtered out)", awake);

  // 4. Fluent query builder. query().with<>().without<>() accumulates the same
  // include / exclude lists at compile time and reads identically to a sentence.
  // .each can run straight off the builder. The filter set is part of the type,
  // so an empty result from a typo is impossible: it would not compile.
  std::println("== 4. query().with<position>().with<velocity>().without<sleeping>() ==");
  int queried{0};
  reg.query().with<position>().with<velocity>().without<sleeping>().each(
    [&queried](position const& p, velocity const&) noexcept {
      ++queried;
      std::println("  awake mover now at x={:.1f}", p.x);
    }
  );
  std::println("  query matched: {}", queried);

  // 5. Range-for over a view. A view is a range, so structured bindings unpack
  // (entity, components...) per step. Same match set as the .each form; pick
  // whichever reads better. Here we just sum positions without mutating.
  std::println("== 5. range-for over view<position, velocity> ==");
  float sum_x{0.0F};
  for (auto [e, p, v] : reg.view<position, velocity>()) {
    nexenne::utility::discard(e);
    nexenne::utility::discard(v);
    sum_x += p.x;
  }
  std::println("  sum of mover x-coords: {:.1f}", sum_x);

  std::println("\nA view is a system's contract: name the components you touch,");
  std::println("and the registry hands you exactly the matching entities, dense");
  std::println("and intersected, with includes required and excludes forbidden.");
  return 0;
}
