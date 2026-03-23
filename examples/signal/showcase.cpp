/**
 * @file
 * @brief A guided tour of nexenne::signal through one realistic system: the
 *        event plumbing of a tiny game loop.
 *
 * Nothing here renders or sleeps - it wires subscribers to typed signals and
 * fires them, printing what each slot sees, so the module's pieces show up in
 * context rather than in isolation:
 *
 *   1. An event bus       -> a dynamic signal<> published as a connect-only
 *                            sink, so systems subscribe but cannot forge events.
 *   2. Lifetime safety    -> a slot member auto-disconnects a subscriber that
 *                            dies mid-game; a scoped_connection scopes another.
 *   3. Ordering and once  -> priority puts physics before audio; connect_once
 *                            arms a one-shot "first blood" hook.
 *   4. Mid-emit edits     -> a slot disconnects another slot while emit walks
 *                            the list, and emit_blocker mutes a noisy channel.
 *   5. Aggregation        -> emit_and_collect polls every damage modifier and
 *                            folds the returned multipliers into one number.
 *   6. Heap-free variant  -> a static_signal input dispatcher: same API, fixed
 *                            capacity, zero allocation, connect-past-bound fails.
 *
 * The thread running through it: why a signal beats a hand-rolled
 * std::vector<std::function>. A manual callback list makes YOU prove three
 * things on every edit - that a dead subscriber is removed before it is called
 * (lifetime), that systems do not reach into each other to unsubscribe
 * (decoupling), and that firing while editing the list does not corrupt it
 * (reentrancy). A signal proves all three for you. Read it top to bottom.
 */

#include <cstdint>
#include <print>
#include <string_view>

#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/signal.hpp>
#include <nexenne/signal/slot.hpp>
#include <nexenne/signal/static_signal.hpp>

namespace sig = nexenne::signal;

namespace {

// A damage event carried by reference: the signal parameter is a const& so the
// struct is never copied per slot (see the forwarding note in signal.hpp).
struct damage_event {
  std::string_view source{};
  int amount{0};
};

// The game's central event bus. It owns the signals and hands out connect-only
// sinks, so any system can subscribe but only the bus can fire. This is the
// decoupling win: a subscriber names the bus, never the other subscribers, so
// adding or removing a system touches exactly one site.
class event_bus {
public:
  [[nodiscard]] auto on_damage() noexcept -> sig::sink<void(damage_event const&)> {
    return m_on_damage.as_sink();
  }

  auto deal_damage(damage_event const& ev) noexcept -> void {
    m_on_damage.emit(ev);
  }

  // The damage channel is exposed so a scope can mute it with an emit_blocker;
  // emit_blocker needs the signal itself, not a sink, since it calls block().
  [[nodiscard]] auto damage_signal() noexcept -> sig::signal<void(damage_event const&)>& {
    return m_on_damage;
  }

private:
  sig::signal<void(damage_event const&)> m_on_damage{};
};

// An enemy that subscribes with a member function tracked by a slot. When the
// enemy dies its slot destructor disconnects the subscription, so the bus never
// calls into a freed object - the lifetime guarantee a raw callback list cannot
// give without bookkeeping you have to get right by hand.
class enemy {
public:
  enemy(std::string_view name, sig::sink<void(damage_event const&)> dmg) noexcept : m_name{name} {
    dmg.connect<&enemy::take_hit>(*this, m_slot);
  }

  auto take_hit(damage_event const& ev) noexcept -> void {
    m_hp -= ev.amount;
    std::println("  {} takes {} from {} (hp now {})", m_name, ev.amount, ev.source, m_hp);
  }

private:
  std::string_view m_name{};
  int m_hp{100};
  sig::slot<2> m_slot{};  // auto-disconnects every tracked subscription on death
};

}  // namespace

auto main() -> int {
  auto bus{event_bus{}};

  // 1. The HUD logs every hit. A plain captureless lambda is stored as a raw
  // function pointer (no type-erasure thunk), so emit is one indirect call.
  std::println("== 1. Event bus ==");
  auto const hud{bus.on_damage().connect([](damage_event const& ev) noexcept {
    std::println("  HUD: -{} hp", ev.amount);
  })};
  static_cast<void>(hud);

  // 2. Priority ties systems into a fixed order without them knowing about each
  // other: physics (priority -10) must settle the hit before audio (priority 5)
  // reacts to it. Lower fires first; equal priorities keep insertion order.
  std::println("== 2. Ordered systems ==");
  auto const physics{bus.on_damage().connect(
    [](damage_event const&) noexcept { std::println("  physics: apply knockback"); }, -10
  )};
  auto const audio{bus.on_damage().connect(
    [](damage_event const&) noexcept { std::println("  audio: play 'hit' sound"); }, 5
  )};
  static_cast<void>(physics);
  static_cast<void>(audio);

  // A one-shot achievement hook: connect_once arms it for exactly one emit, then
  // the slot sweeps itself. No wrapper, no manual "remove me after firing" flag.
  auto const first_blood{bus.on_damage().connect_once([](damage_event const& ev) noexcept {
    std::println("  achievement: first blood by {}!", ev.source);
  })};
  static_cast<void>(first_blood);

  // 3. Lifetime safety. The goblin lives only inside this scope; when it dies,
  // its tracked subscription disappears with it. The bus keeps firing afterward
  // with no dangling call - exactly the bug a hand-maintained list invites.
  std::println("== 3. Lifetime-tied subscriber ==");
  {
    auto goblin{enemy{"goblin", bus.on_damage()}};
    std::println("first attack (goblin alive):");
    bus.deal_damage(damage_event{.source = "player", .amount = 12});  // first_blood fires here

    std::println("second attack (goblin alive, achievement already gone):");
    bus.deal_damage(damage_event{.source = "player", .amount = 8});
  }  // goblin dies: its slot disconnects take_hit before the next emit

  std::println("third attack (goblin gone, bus still safe):");
  bus.deal_damage(damage_event{.source = "trap", .amount = 5});

  // 4. A scoped_connection: RAII disconnect with no member to declare. Handy for
  // a transient subscriber - a buff, a cutscene listener - that lives for a
  // block rather than for an object's lifetime.
  std::println("== 4. Scoped + blocked emission ==");
  {
    auto const shield{sig::scoped_connection{bus.on_damage().connect(
      [](damage_event const& ev) noexcept { std::println("  shield absorbs {}", ev.amount); }
    )}};
    static_cast<void>(shield);

    // emit_blocker mutes the whole damage channel for a scope (an invulnerability
    // frame, say) and restores the prior state on exit - nesting-correct, unlike
    // a bare block()/unblock() pair that an early return could leave stuck.
    {
      auto const iframe{sig::emit_blocker{bus.damage_signal()}};
      std::println("invuln frame (this emit is suppressed):");
      bus.deal_damage(damage_event{.source = "spikes", .amount = 99});  // nothing prints
    }
    std::println("after invuln (shield active):");
    bus.deal_damage(damage_event{.source = "fireball", .amount = 7});
  }  // shield's scoped_connection disconnects here

  // 5. Reentrancy: a slot disconnects another slot while emit walks the list.
  // A signal defers the removal to the end of the outermost emit, so the
  // in-progress iteration stays valid - a hand-rolled list erasing mid-loop
  // would invalidate its own iterator. Here a one-time trap fires, then unhooks
  // a "trap armed" indicator that was connected before it.
  std::println("== 5. Disconnect mid-emit ==");
  auto indicator{bus.on_damage().connect(
    [](damage_event const&) noexcept { std::println("  indicator: trap is armed"); }, -1
  )};
  auto const spring{bus.on_damage().connect_once([&indicator](damage_event const&) noexcept {
    std::println("  trap springs and disarms the indicator");
    static_cast<void>(indicator.disconnect());  // safe: deferred to emit end
  })};
  static_cast<void>(spring);
  std::println("trigger the trap:");
  bus.deal_damage(damage_event{.source = "tripwire", .amount = 3});
  std::println("next hit (indicator already gone, trap spent):");
  bus.deal_damage(damage_event{.source = "player", .amount = 4});

  // 6. Return-value aggregation. A signal whose slots return a value lets
  // emit_and_collect gather every result in fire order. Here each modifier
  // reports a damage multiplier and we fold them into a final factor - one query
  // fans out to every registered modifier with no central table to maintain.
  std::println("== 6. Aggregated damage modifiers ==");
  auto modifiers{sig::signal<double(int)>{}};
  auto const crit{modifiers.connect([](int base) noexcept { return base > 5 ? 2.0 : 1.0; })};
  auto const vuln{modifiers.connect([](int) noexcept { return 1.5; })};
  auto const armor{modifiers.connect([](int) noexcept { return 0.8; })};
  static_cast<void>(crit);
  static_cast<void>(vuln);
  static_cast<void>(armor);
  auto const factors{modifiers.emit_and_collect(10)};
  auto product{1.0};
  for (auto const f : factors) {
    product *= f;
  }
  std::println("  collected {} multipliers, product {:.2f}x", factors.size(), product);

  // 7. The heap-free sibling: static_signal. Same connect/emit/priority API, but
  // all slot storage lives inline - zero allocation, a footprint you can size at
  // compile time. The trade is a fixed capacity: a connect past MaxSlots fails
  // (returns an invalid handle) instead of growing. Ideal for an input
  // dispatcher on a tight target where the action count is known up front.
  std::println("== 7. Heap-free input dispatcher ==");
  auto input{sig::static_signal<void(std::uint8_t), 3>{}};
  auto const move{input.connect([](std::uint8_t key) noexcept {
    std::println("  move handler sees key {}", key);
  })};
  auto const fire{input.connect(
    [](std::uint8_t) noexcept { std::println("  fire handler triggers"); }, -1  // fires first
  )};
  auto menu{input.connect([](std::uint8_t) noexcept { std::println("  menu toggles"); })};
  static_cast<void>(move);
  static_cast<void>(fire);

  std::println("dispatcher full at capacity {}: {}", input.capacity(), input.full());
  auto const overflow{input.connect([](std::uint8_t) noexcept {})};  // no room: signal is full
  std::println("  connect past the bound succeeded? {}", overflow.has_target());

  std::println("dispatch key 32 (fire first by priority):");
  input.emit(std::uint8_t{32});

  // Free a slot, then reuse it. Disconnecting menu opens one of the three fixed
  // slots; the transient connect below then takes that freed slot (no allocation,
  // just a slot flipped back to free - full() goes false, then true again). A
  // static_scoped_connection tears that subscription down by scope, the heap-free
  // counterpart of scoped_connection. It must not outlive the signal, since a
  // token handle cannot detect the signal's death.
  menu.disconnect();
  std::println("menu disconnected; a slot is free again: full() = {}", input.full());
  {
    auto transient{sig::static_scoped_connection{input.connect([](std::uint8_t) noexcept {
      std::println("  transient input hook");
    })}};
    static_cast<void>(transient);
    std::println("transient took the freed slot; full() = {}", input.full());
    std::println("dispatch key 13 (transient now in the pool, menu gone):");
    input.emit(std::uint8_t{13});
  }  // transient disconnects here, freeing the slot back to the fixed pool

  std::println("after the transient hook left:");
  input.emit(std::uint8_t{7});

  std::println("\nThat is the module in one game loop: a published bus, lifetime");
  std::println("safety from slot and scoped_connection, priority and once ordering,");
  std::println("reentrant mid-emit edits, blocked channels, collected returns, and");
  std::println("the same API again with zero heap in static_signal.");
  return 0;
}
