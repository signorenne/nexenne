/**
 * @file
 * @brief A focused tour of entity and component lifecycle: create / destroy,
 *        add / remove / patch, and generation-safe handles.
 *
 * No simulation here, just the bookkeeping primitives every ECS leans on:
 *
 *   1. create() mints a live handle; valid() reports liveness. A
 *      default-constructed handle (generation 0) is never valid, and the
 *      entity_id formatter prints a handle as entity(index, generation).
 *   2. add / has / get / remove manage a component on an entity. add returns
 *      true for a new component and false for a replacement; get yields a
 *      reference wrapper or container::container_error, never a null pointer.
 *      all_of / any_of fold several has checks, tag components included.
 *   3. patch mutates a component in place and fires on_update, whose
 *      listener sees the value after the mutation.
 *   4. destroy() tears down every component and bumps the generation, so a
 *      stale handle to a recycled slot reads as invalid (no dangling ids).
 *   5. clear() wipes everything and invalidates all outstanding handles.
 *
 * The generation counter is the whole point of a recycle-safe registry: indices
 * are reused, but each reuse bumps the generation, so an old handle never
 * silently addresses the new occupant of its slot. Read it top to bottom.
 */

#include <print>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/utility/ignore.hpp>

namespace ecs = nexenne::ecs;

namespace {

struct name {
  int tag{};
};

struct mark {};

}  // namespace

auto main() -> int {
  auto reg{ecs::registry{}};

  std::println("== 1. Create and validity ==");
  auto const e{reg.create()};
  std::println("  created entity      {}", e);
  std::println("  valid(e)            {}", reg.valid(e));
  std::println("  valid(default id)   {}", reg.valid(ecs::entity_id{}));
  std::println("  alive               {}", reg.alive());

  std::println("== 2. Component add / has / get / remove ==");
  std::println("  add<name> (new)     {}", reg.add<name>(e, {7}));
  std::println("  add<name> (replace) {}", reg.add<name>(e, {8}));
  std::println("  has<name>           {}", reg.has<name>(e));
  if (auto const n{reg.get<name>(e)}) {
    std::println("  get<name>.tag       {}", n->get().tag);
  }
  std::println("  remove<name>        {}", reg.remove<name>(e));
  std::println("  has<name> after rm  {}", reg.has<name>(e));
  std::println("  get<name> missing?  {}", !reg.get<name>(e).has_value());

  reg.add<name>(e, {9});
  reg.add<mark>(e, {});
  std::println("  all_of<name, mark>  {}", reg.all_of<name, mark>(e));
  std::println("  any_of<mark>        {}", reg.any_of<mark>(e));

  std::println("== 3. Patch and on_update ==");
  [[maybe_unused]] auto update_log{
    reg.on_update<name>().connect([](ecs::entity_id const who, name const& n) noexcept {
      std::println("  [on_update] entity {} now tag {}", who.index(), n.tag);
    })
  };
  reg.patch<name>(e, [](name& n) noexcept { n.tag += 100; });

  std::println("== 4. Destroy and recycling ==");
  auto const old_index{e.index()};
  auto const old_gen{e.generation()};
  std::println("  destroy(e)          {}", reg.destroy(e));
  std::println("  valid(e) after      {}", reg.valid(e));

  auto const recycled{reg.create()};
  std::println(
    "  recycled index      {} (same slot? {})", recycled.index(), recycled.index() == old_index
  );
  std::println(
    "  recycled gen        {} (bumped? {})", recycled.generation(), recycled.generation() != old_gen
  );
  std::println("  stale handle valid? {}", reg.valid(e));
  std::println("  fresh handle valid? {}", reg.valid(recycled));

  std::println("== 5. Clear ==");
  reg.add<name>(recycled, {42});
  std::println("  alive before clear  {}", reg.alive());
  reg.clear();
  std::println("  alive after clear   {}", reg.alive());
  std::println("  recycled valid?     {}", reg.valid(recycled));

  std::println("\nGeneration-tagged handles make destroy safe: indices recycle,");
  std::println("but stale ids never address the slot's new owner.");
  return 0;
}
