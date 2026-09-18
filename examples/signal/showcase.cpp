/**
 * @file
 * @brief A guided tour of nexenne::signal: the event plumbing of a tiny game loop.
 *
 * Nothing here renders or sleeps: it wires subscribers to typed signals and
 * fires them, printing what each slot sees, so the module's pieces show up in
 * context rather than in isolation.
 *
 * The thread running through it: why a signal beats a hand-rolled
 * std::vector<std::function>. A manual callback list makes YOU prove three
 * things on every edit: that a dead subscriber is removed before it is called
 * (lifetime), that systems do not reach into each other to unsubscribe
 * (decoupling), and that firing while editing the list does not corrupt it
 * (reentrancy). A signal proves all three for you.
 *
 * The program walks seven steps:
 *
 * 1. An event bus. The bus owns the signals and publishes connect-only sinks,
 *    so any system can subscribe but only the bus can fire: a subscriber names
 *    the bus, never the other subscribers, so adding or removing a system
 *    touches one site. Damage events travel by const reference and are never
 *    copied per slot. The HUD's captureless lambda is stored as a raw function
 *    pointer, so its emit is one indirect call.
 * 2. Ordered systems. Priority puts physics (-10) before audio (5) without
 *    either knowing about the other; lower fires first and ties keep insertion
 *    order. connect_once arms a "first blood" hook for exactly one emit, after
 *    which the slot sweeps itself, with no manual "remove me" flag.
 * 3. A lifetime-tied subscriber. The goblin's hit handler is tracked by its slot
 *    member, so when the goblin dies at the end of its scope the subscription
 *    goes with it and later emits make no dangling call, exactly the bug a
 *    hand-maintained list invites.
 * 4. Scoped and blocked emission. A scoped_connection disconnects a transient
 *    subscriber (a buff, a cutscene listener) at the end of a block, with no
 *    member to declare. An emit_blocker mutes the damage channel for an
 *    invulnerability frame and restores the prior state on exit, nesting-correct
 *    where a bare block() and unblock() pair could be left stuck by an early
 *    return; it needs the signal itself, not a sink, since it calls block().
 * 5. Disconnect mid-emit. A one-shot trap disarms an indicator connected before
 *    it while emit walks the list; the signal defers the removal to the end of
 *    the outermost emit, where a hand-rolled list erasing mid-loop would
 *    invalidate its own iterator.
 * 6. Aggregation. emit_and_collect gathers every modifier's damage multiplier in
 *    fire order and the program folds them into one factor: one query fans out
 *    to every responder with no central table to maintain.
 * 7. The heap-free sibling. static_signal keeps the same connect, emit and
 *    priority API with all slot storage inline: zero allocation and a footprint
 *    sized at compile time. The trade is a fixed capacity, so a connect past
 *    MaxSlots returns an invalid handle instead of growing. Disconnecting one
 *    handler frees a slot for a transient hook held by a
 *    static_scoped_connection, which must not outlive the signal, since a token
 *    handle cannot detect the signal's death.
 */

#include <cstdint>
#include <print>
#include <string_view>

#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/signal.hpp>
#include <nexenne/signal/slot.hpp>
#include <nexenne/signal/static_signal.hpp>
#include <nexenne/utility/ignore.hpp>

namespace sig = nexenne::signal;

namespace {

/// @brief A damage event, emitted by const reference so no slot copies it.
struct damage_event {
  std::string_view source{};  ///< Who dealt the damage.
  int amount{0};              ///< Hit points removed.
};

/// @brief The game's central event bus: it fires the signals and publishes connect-only sinks.
class event_bus {
public:
  [[nodiscard]] auto on_damage() noexcept -> sig::sink<void(damage_event const&)> {
    return m_on_damage.as_sink();
  }

  auto deal_damage(damage_event const& ev) noexcept -> void {
    m_on_damage.emit(ev);
  }

  [[nodiscard]] auto damage_signal() noexcept -> sig::signal<void(damage_event const&)>& {
    return m_on_damage;
  }

private:
  sig::signal<void(damage_event const&)> m_on_damage{};
};

/// @brief An enemy whose slot member disconnects its hit handler when it dies.
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
  sig::slot<2> m_slot{};  ///< Disconnects every tracked subscription on destruction.
};

}  // namespace

auto main() -> int {
  auto bus{event_bus{}};

  std::println("== 1. Event bus ==");
  [[maybe_unused]] auto const hud{bus.on_damage().connect([](damage_event const& ev) noexcept {
    std::println("  HUD: -{} hp", ev.amount);
  })};

  std::println("== 2. Ordered systems ==");
  [[maybe_unused]] auto const physics{bus.on_damage().connect(
    [](damage_event const&) noexcept { std::println("  physics: apply knockback"); }, -10
  )};
  [[maybe_unused]] auto const audio{bus.on_damage().connect(
    [](damage_event const&) noexcept { std::println("  audio: play 'hit' sound"); }, 5
  )};

  [[maybe_unused]] auto const first_blood{
    bus.on_damage().connect_once([](damage_event const& ev) noexcept {
      std::println("  achievement: first blood by {}!", ev.source);
    })
  };

  std::println("== 3. Lifetime-tied subscriber ==");
  {
    auto goblin{enemy{"goblin", bus.on_damage()}};
    std::println("first attack (goblin alive):");
    bus.deal_damage(damage_event{.source = "player", .amount = 12});

    std::println("second attack (goblin alive, achievement already gone):");
    bus.deal_damage(damage_event{.source = "player", .amount = 8});
  }

  std::println("third attack (goblin gone, bus still safe):");
  bus.deal_damage(damage_event{.source = "trap", .amount = 5});

  std::println("== 4. Scoped + blocked emission ==");
  {
    [[maybe_unused]] auto const shield{sig::scoped_connection{bus.on_damage().connect(
      [](damage_event const& ev) noexcept { std::println("  shield absorbs {}", ev.amount); }
    )}};

    {
      auto const iframe{sig::emit_blocker{bus.damage_signal()}};
      std::println("invuln frame (this emit is suppressed):");
      bus.deal_damage(damage_event{.source = "spikes", .amount = 99});
    }
    std::println("after invuln (shield active):");
    bus.deal_damage(damage_event{.source = "fireball", .amount = 7});
  }

  std::println("== 5. Disconnect mid-emit ==");
  auto indicator{bus.on_damage().connect(
    [](damage_event const&) noexcept { std::println("  indicator: trap is armed"); }, -1
  )};
  [[maybe_unused]] auto const spring{
    bus.on_damage().connect_once([&indicator](damage_event const&) noexcept {
      std::println("  trap springs and disarms the indicator");
      nexenne::utility::ignore(indicator.disconnect());
    })
  };
  std::println("trigger the trap:");
  bus.deal_damage(damage_event{.source = "tripwire", .amount = 3});
  std::println("next hit (indicator already gone, trap spent):");
  bus.deal_damage(damage_event{.source = "player", .amount = 4});

  std::println("== 6. Aggregated damage modifiers ==");
  auto modifiers{sig::signal<double(int)>{}};
  [[maybe_unused]] auto const crit{modifiers.connect([](int base) noexcept {
    return base > 5 ? 2.0 : 1.0;
  })};
  [[maybe_unused]] auto const vuln{modifiers.connect([](int) noexcept { return 1.5; })};
  [[maybe_unused]] auto const armor{modifiers.connect([](int) noexcept { return 0.8; })};
  auto const factors{modifiers.emit_and_collect(10)};
  auto product{1.0};
  for (auto const f : factors) {
    product *= f;
  }
  std::println("  collected {} multipliers, product {:.2f}x", factors.size(), product);

  std::println("== 7. Heap-free input dispatcher ==");
  auto input{sig::static_signal<void(std::uint8_t), 3>{}};
  [[maybe_unused]] auto const move{input.connect([](std::uint8_t key) noexcept {
    std::println("  move handler sees key {}", key);
  })};
  [[maybe_unused]] auto const fire{
    input.connect([](std::uint8_t) noexcept { std::println("  fire handler triggers"); }, -1)
  };
  auto menu{input.connect([](std::uint8_t) noexcept { std::println("  menu toggles"); })};

  std::println("dispatcher full at capacity {}: {}", input.capacity(), input.full());
  auto const overflow{input.connect([](std::uint8_t) noexcept {})};
  std::println("  connect past the bound succeeded? {}", overflow.has_target());

  std::println("dispatch key 32 (fire first by priority):");
  input.emit(std::uint8_t{32});

  menu.disconnect();
  std::println("menu disconnected; a slot is free again: full() = {}", input.full());
  {
    [[maybe_unused]] auto transient{sig::static_scoped_connection{
      input.connect([](std::uint8_t) noexcept { std::println("  transient input hook"); })
    }};
    std::println("transient took the freed slot; full() = {}", input.full());
    std::println("dispatch key 13 (transient now in the pool, menu gone):");
    input.emit(std::uint8_t{13});
  }

  std::println("after the transient hook left:");
  input.emit(std::uint8_t{7});

  std::println("\nThat is the module in one game loop: a published bus, lifetime");
  std::println("safety from slot and scoped_connection, priority and once ordering,");
  std::println("reentrant mid-emit edits, blocked channels, collected returns, and");
  std::println("the same API again with zero heap in static_signal.");
  return 0;
}
