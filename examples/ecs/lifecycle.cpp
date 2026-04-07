/**
 * @file
 * @brief A focused tour of entity and component lifecycle: create / destroy,
 *        add / remove / patch, and generation-safe handles.
 *
 * No simulation here, just the bookkeeping primitives every ECS leans on:
 *
 *   1. create() mints a live handle; valid() reports liveness.
 *   2. add / has / get / remove manage a component on an entity.
 *   3. patch mutates a component in place and fires on_update.
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
#include <nexenne/utility/discard.hpp>

namespace ecs = nexenne::ecs;

namespace {

struct name {
  int tag{};
};

struct mark {};  // empty tag component

}  // namespace

auto main() -> int {
  auto reg{ecs::registry{}};

  // 1. Create and validity. A fresh handle is live; a default-constructed one
  // (generation 0) never is, so an uninitialised id is always safely rejected.
  std::println("== 1. Create and validity ==");
  auto const e{reg.create()};
  std::println("  created entity index {} gen {}", e.index(), e.generation());
  std::println("  valid(e)            {}", reg.valid(e));
  std::println("  valid(default id)   {}", reg.valid(ecs::entity_id{}));
  std::println("  alive               {}", reg.alive());

  // 2. Add / has / get / remove. add returns true when it attaches a NEW
  // component and false when it overwrites an existing one. get follows the
  // library's expected-style error policy: a hit yields a reference wrapper, a
  // miss yields container_error, so the success path needs no nullptr check.
  std::println("== 2. Component add / has / get / remove ==");
  std::println("  add<name> (new)     {}", reg.add<name>(e, {7}));
  std::println("  add<name> (replace) {}", reg.add<name>(e, {8}));
  std::println("  has<name>           {}", reg.has<name>(e));
  if (auto const n{reg.get<name>(e)}) {
    std::println("  get<name>.tag       {}", n->get().tag);
  }
  std::println("  remove<name>        {}", reg.remove<name>(e));
  std::println("  has<name> after rm  {}", reg.has<name>(e));
  // get on the now-missing component reports not-found rather than crashing.
  std::println("  get<name> missing?  {}", !reg.get<name>(e).has_value());

  // all_of / any_of fold several has<> checks. Tag components participate just
  // like data components: presence is all that matters.
  reg.add<name>(e, {9});
  reg.add<mark>(e, {});
  std::println("  all_of<name, mark>  {}", reg.all_of<name, mark>(e));
  std::println("  any_of<mark>        {}", reg.any_of<mark>(e));

  // 3. Patch: mutate in place and notify. A patch listener observes the value
  // AFTER the mutation. We wire on_update<name> first, then patch.
  std::println("== 3. Patch and on_update ==");
  auto update_log{
    reg.on_update<name>().connect([](ecs::entity_id const who, name const& n) noexcept {
      std::println("  [on_update] entity {} now tag {}", who.index(), n.tag);
    })
  };
  nexenne::utility::discard(update_log);
  reg.patch<name>(e, [](name& n) noexcept { n.tag += 100; });

  // 4. Destroy and generation safety. Capture the slot index, destroy the
  // entity, then create a new one. The free list hands back the SAME index, but
  // the generation has advanced, so the old handle no longer matches.
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
  // The stale handle e and the fresh handle recycled share a slot but differ in
  // generation, so the registry never confuses them.
  std::println("  stale handle valid? {}", reg.valid(e));
  std::println("  fresh handle valid? {}", reg.valid(recycled));

  // 5. clear wipes every entity and bumps every used slot's generation, so even
  // a handle that was live a moment ago is now invalid forever.
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
