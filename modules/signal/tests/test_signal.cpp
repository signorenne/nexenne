#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/signal/connection.hpp>
#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/signal.hpp>
#include <nexenne/signal/slot.hpp>

namespace {

using nexenne::signal::connection;
using nexenne::signal::scoped_connection;
using nexenne::signal::signal;

TEST_CASE("nexenne::signal::signal empty by default") {
  auto sig{signal<void()>{}};
  CHECK(sig.empty());
  CHECK(sig.size() == 0);
}

TEST_CASE("nexenne::signal::signal connect / emit fires the slot") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto conn{sig.connect([&] noexcept { ++count; })};
  CHECK(sig.size() == 1);

  sig.emit();
  CHECK(count == 1);

  sig.emit();
  CHECK(count == 2);

  (void)conn;
}

TEST_CASE("nexenne::signal::signal operator() is an alias for emit") {
  auto sig{signal<void(int)>{}};
  auto seen{0};
  auto conn{sig.connect([&](int n) noexcept { seen += n; })};

  sig(3);
  sig(4);
  CHECK(seen == 7);

  (void)conn;
}

TEST_CASE("nexenne::signal::signal emit with arguments forwards them") {
  auto sig{signal<void(int, int)>{}};
  auto sum{0};
  auto conn{sig.connect([&](int a, int b) noexcept { sum = a + b; })};

  sig.emit(3, 4);
  CHECK(sum == 7);

  sig.emit(100, 200);
  CHECK(sum == 300);

  (void)conn;
}

TEST_CASE("nexenne::signal::signal forwards a reference argument so slots can mutate it") {
  auto sig{signal<void(int&)>{}};
  auto conn{sig.connect([](int& n) noexcept { n *= 2; })};

  auto value{5};
  sig.emit(value);
  CHECK(value == 10);

  (void)conn;
}

TEST_CASE("nexenne::signal::signal reference mutation chains across multiple slots") {
  auto sig{signal<void(int&)>{}};
  auto c1{sig.connect([](int& n) noexcept { n += 1; })};
  auto c2{sig.connect([](int& n) noexcept { n *= 10; })};

  auto value{2};
  sig.emit(value);
  // First slot makes 3, second makes 30: each sees the previous slot's mutation.
  CHECK(value == 30);

  (void)c1;
  (void)c2;
}

TEST_CASE("nexenne::signal::signal supports multiple slots in connection order") {
  auto sig{signal<void(int)>{}};
  auto log{std::vector<int>{}};

  auto c1{sig.connect([&](int n) noexcept { log.push_back(n * 1); })};
  auto c2{sig.connect([&](int n) noexcept { log.push_back(n * 2); })};
  auto c3{sig.connect([&](int n) noexcept { log.push_back(n * 3); })};

  sig.emit(5);
  REQUIRE(log.size() == 3);
  CHECK(log[0] == 5);
  CHECK(log[1] == 10);
  CHECK(log[2] == 15);

  (void)c1;
  (void)c2;
  (void)c3;
}

TEST_CASE("nexenne::signal::signal empty emit is a no-op") {
  auto sig{signal<void()>{}};
  sig.emit();  // no slots, must not crash
  CHECK(sig.empty());

  auto sig_args{signal<void(int)>{}};
  sig_args.emit(7);  // no slots with args either
  CHECK(sig_args.empty());
}

TEST_CASE("nexenne::signal::signal empty emit_and_collect collects nothing") {
  auto sig{signal<int()>{}};
  auto const results{sig.emit_and_collect()};
  CHECK(results.empty());
}

TEST_CASE("nexenne::signal::connection.disconnect removes the slot") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto conn{sig.connect([&] noexcept { ++count; })};

  sig.emit();
  CHECK(count == 1);

  CHECK(conn.disconnect());
  CHECK(sig.size() == 0);

  sig.emit();
  CHECK(count == 1);  // unchanged

  CHECK_FALSE(conn.disconnect());  // already disconnected
}

TEST_CASE("nexenne::signal::connection double-disconnect and copy semantics") {
  auto sig{signal<void()>{}};
  auto conn{sig.connect([] noexcept {})};
  auto copy{conn};  // copies share the same logical connection

  CHECK(conn.disconnect());        // first wins
  CHECK_FALSE(copy.disconnect());  // the copy now reports already-disconnected
  CHECK_FALSE(conn.disconnect());  // and the original on a second call too
}

TEST_CASE("nexenne::signal::connection is valid until signal is destroyed") {
  auto sig{std::optional<signal<void()>>{std::in_place}};
  auto conn{sig->connect([] noexcept {})};
  CHECK(conn.valid());

  sig.reset();
  CHECK_FALSE(conn.valid());
  // Disconnect on dead signal is a safe no-op.
  CHECK_FALSE(conn.disconnect());
}

TEST_CASE("nexenne::signal::connection disconnect of an already-dead connection is safe") {
  auto conn{connection{}};
  // A default connection refers to nothing; disconnect must be a no-op.
  CHECK_FALSE(conn.disconnect());
  CHECK_FALSE(conn.disconnect());
  CHECK_FALSE(conn.valid());
}

TEST_CASE("nexenne::signal::scoped_connection disconnects on destruction") {
  auto sig{signal<void()>{}};
  auto count{0};
  {
    auto sc{scoped_connection{sig.connect([&] noexcept { ++count; })}};
    sig.emit();
    CHECK(count == 1);
  }
  sig.emit();
  CHECK(count == 1);  // scoped_connection destructor ran
}

TEST_CASE(
  "nexenne::signal::scoped_connection move transfers ownership (only destination disconnects)"
) {
  auto sig{signal<void()>{}};
  auto count{0};
  auto outer{scoped_connection{}};
  {
    auto inner{scoped_connection{sig.connect([&] noexcept { ++count; })}};
    outer = std::move(inner);  // ownership transferred to outer
  }
  // inner destroyed (now empty); outer still owns -> slot still connected
  sig.emit();
  CHECK(count == 1);
  CHECK(sig.size() == 1);
}

TEST_CASE("nexenne::signal::scoped_connection move-construct transfers ownership") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto holder{std::optional<scoped_connection>{}};
  {
    auto inner{scoped_connection{sig.connect([&] noexcept { ++count; })}};
    holder.emplace(std::move(inner));  // move-construct out of inner
  }  // ~inner is now inert
  sig.emit();
  CHECK(count == 1);  // moved-from inner did NOT disconnect
  holder.reset();     // destination disconnects here
  sig.emit();
  CHECK(count == 1);
}

TEST_CASE("nexenne::signal::scoped_connection move-assign disconnects the prior connection") {
  auto sig{signal<void()>{}};
  auto a_fired{0};
  auto b_fired{0};
  auto dst{scoped_connection{sig.connect([&] noexcept { ++a_fired; })}};
  {
    auto src{scoped_connection{sig.connect([&] noexcept { ++b_fired; })}};
    CHECK(sig.size() == 2);
    dst = std::move(src);  // dst's old (a) connection must disconnect now
  }
  CHECK(sig.size() == 1);  // only b survives
  sig.emit();
  CHECK(a_fired == 0);  // a was disconnected by the move-assign
  CHECK(b_fired == 1);
}

TEST_CASE("nexenne::signal::scoped_connection self-move-assign is safe") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto sc{scoped_connection{sig.connect([&] noexcept { ++fired; })}};
  auto& ref{sc};
  ref = std::move(sc);  // self-move must not disconnect
  sig.emit();
  CHECK(fired == 1);  // still connected
  CHECK(sc.valid());
}

TEST_CASE("nexenne::signal::scoped_connection.release returns the connection") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto raw_conn{connection{}};
  {
    auto sc{scoped_connection{sig.connect([&] noexcept { ++count; })}};
    raw_conn = sc.release();  // sc no longer auto-disconnects
    CHECK_FALSE(sc.valid());  // released: scope holds nothing
  }
  sig.emit();
  CHECK(count == 1);  // still connected

  raw_conn.disconnect();
  sig.emit();
  CHECK(count == 1);
}

TEST_CASE("nexenne::signal::scoped_connection.disconnect ends early and get() observes it") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto sc{scoped_connection{sig.connect([&] noexcept { ++fired; })}};
  CHECK(sc.get().valid());

  CHECK(sc.disconnect());
  sig.emit();
  CHECK(fired == 0);
  CHECK_FALSE(sc.disconnect());  // already disconnected
}

TEST_CASE("nexenne::signal::signal disconnect_all clears every slot") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto c1{sig.connect([&] noexcept { ++count; })};
  auto c2{sig.connect([&] noexcept { ++count; })};
  auto c3{sig.connect([&] noexcept { ++count; })};
  CHECK(sig.size() == 3);

  sig.disconnect_all();
  CHECK(sig.empty());

  sig.emit();
  CHECK(count == 0);

  (void)c1;
  (void)c2;
  (void)c3;
}

// priority ordering

TEST_CASE("nexenne::signal::signal slot priority orders invocation") {
  auto sig{signal<void()>{}};
  auto log{std::vector<int>{}};

  auto c_low{sig.connect([&] noexcept { log.push_back(2); }, 10)};   // last
  auto c_mid{sig.connect([&] noexcept { log.push_back(1); }, 0)};    // middle
  auto c_high{sig.connect([&] noexcept { log.push_back(0); }, -5)};  // first

  sig.emit();
  REQUIRE(log.size() == 3);
  CHECK(log[0] == 0);  // priority -5 fires first
  CHECK(log[1] == 1);  // priority 0
  CHECK(log[2] == 2);  // priority 10

  (void)c_low;
  (void)c_mid;
  (void)c_high;
}

TEST_CASE("nexenne::signal::signal same priority preserves insertion order (stable sort)") {
  auto sig{signal<void()>{}};
  auto log{std::vector<int>{}};
  auto c1{sig.connect([&] noexcept { log.push_back(1); }, 0)};
  auto c2{sig.connect([&] noexcept { log.push_back(2); }, 0)};
  auto c3{sig.connect([&] noexcept { log.push_back(3); }, 0)};

  sig.emit();
  REQUIRE(log.size() == 3);
  CHECK(log[0] == 1);
  CHECK(log[1] == 2);
  CHECK(log[2] == 3);

  (void)c1;
  (void)c2;
  (void)c3;
}

TEST_CASE("nexenne::signal::signal mixed priorities with ties keep insertion order within a tier") {
  // Insertion-bubble path: equal priorities never swap, so a tie keeps the
  // order the slots were connected in, regardless of surrounding priorities.
  auto sig{signal<void()>{}};
  auto order{std::vector<int>{}};
  auto c1{sig.connect([&] { order.push_back(1); }, 10)};
  auto c2{sig.connect([&] { order.push_back(2); }, 5)};
  auto c3{sig.connect([&] { order.push_back(3); }, 15)};
  auto c4{sig.connect([&] { order.push_back(4); }, 5)};  // tied with c2: comes after
  sig.emit();
  CHECK(order == std::vector{2, 4, 1, 3});

  (void)c1;
  (void)c2;
  (void)c3;
  (void)c4;
}

// one-shot

TEST_CASE("nexenne::signal::signal connect_once fires exactly once") {
  auto sig{signal<void(int)>{}};
  auto seen{std::vector<int>{}};
  auto conn{sig.connect_once([&](int n) noexcept { seen.push_back(n); })};

  sig.emit(1);
  sig.emit(2);
  sig.emit(3);

  REQUIRE(seen.size() == 1);
  CHECK(seen[0] == 1);
  CHECK(sig.empty());
  (void)conn;
}

TEST_CASE("nexenne::signal::signal connect_once handle reports invalid-target after the sweep") {
  auto sig{signal<void()>{}};
  auto conn{sig.connect_once([] noexcept {})};
  CHECK(conn.valid());  // signal alive

  sig.emit();
  CHECK(sig.empty());
  // The slot was swept; a disconnect on the now-removed slot reports false.
  CHECK_FALSE(conn.disconnect());
}

TEST_CASE("nexenne::signal::signal connect_once with stateful capture fires once") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto extra{42};
  auto c{sig.connect_once([&fired, extra] noexcept { fired += extra; })};
  sig.emit();
  sig.emit();
  sig.emit();
  CHECK(fired == 42);
  CHECK(sig.empty());
  (void)c;
}

TEST_CASE(
  "nexenne::signal::signal connect_once does not re-fire when its slot re-emits the same signal"
) {
  // CRITICAL regression: a still-alive once slot would recurse without bound.
  auto sig{signal<void()>{}};
  auto n{0};
  auto c{sig.connect_once([&] noexcept {
    ++n;
    if (n < 5) {   // a still-alive once slot would recurse without bound here
      sig.emit();  // re-entrant emit of the same signal from the once slot
    }
  })};
  (void)c;

  sig.emit();
  CHECK(n == 1);       // fired exactly once despite the re-entrant emit
  CHECK(sig.empty());  // and was swept
}

TEST_CASE("nexenne::signal::signal one-shot fires alongside a persistent slot, then only the "
          "persistent one remains") {
  auto sig{signal<void()>{}};
  auto persistent{0};
  auto once{0};
  auto cp{sig.connect([&] noexcept { ++persistent; })};
  auto co{sig.connect_once([&] noexcept { ++once; })};

  sig.emit();
  CHECK(persistent == 1);
  CHECK(once == 1);
  CHECK(sig.size() == 1);  // once swept, persistent remains

  sig.emit();
  CHECK(persistent == 2);
  CHECK(once == 1);

  (void)cp;
  (void)co;
}

// emit_and_collect

TEST_CASE("nexenne::signal::signal emit_and_collect returns slot results in priority order") {
  auto sig{signal<int(int)>{}};
  auto c1{sig.connect([](int n) noexcept { return n + 1; }, 0)};
  auto c2{sig.connect([](int n) noexcept { return n * 2; }, -1)};  // fires first

  auto results{sig.emit_and_collect(10)};
  REQUIRE(results.size() == 2);
  CHECK(results[0] == 20);  // c2: n*2 (priority -1)
  CHECK(results[1] == 11);  // c1: n+1 (priority 0)

  (void)c1;
  (void)c2;
}

TEST_CASE("nexenne::signal::signal emit_and_collect skips disconnected slots") {
  auto sig{signal<int()>{}};
  auto c1{sig.connect([] noexcept { return 1; })};
  auto c2{sig.connect([] noexcept { return 2; })};
  auto c3{sig.connect([] noexcept { return 3; })};

  c2.disconnect();
  auto results{sig.emit_and_collect()};
  REQUIRE(results.size() == 2);
  CHECK(results[0] == 1);
  CHECK(results[1] == 3);

  (void)c1;
  (void)c3;
}

TEST_CASE("nexenne::signal::signal emit_and_collect returns empty when blocked") {
  auto sig{signal<int()>{}};
  auto c{sig.connect([] noexcept { return 7; })};
  sig.block();
  auto const results{sig.emit_and_collect()};
  CHECK(results.empty());
  (void)c;
}

TEST_CASE("nexenne::signal::signal supports non-void return types (values discarded by emit)") {
  auto sig{signal<int(int)>{}};
  auto last{0};
  auto conn{sig.connect([&](int n) noexcept -> int {
    last = n;
    return n * 2;
  })};
  sig.emit(7);
  CHECK(last == 7);
  // Return value of slot is discarded; emit() returns void.
  (void)conn;
}

// THE REENTRANCY MATRIX

TEST_CASE("nexenne::signal::signal reentrancy (a): a slot connecting a new slot defers it") {
  // The new slot must NOT fire during the current emit and MUST be present after.
  auto sig{signal<void()>{}};
  auto order{std::vector<int>{}};
  auto added{connection{}};
  auto connected{false};
  auto first{sig.connect([&] noexcept {
    order.push_back(1);
    // Connect the deferred slot exactly once (a flag, not a disconnect side
    // effect, so the once-added slot survives to fire on the next emit).
    if (!connected) {
      connected = true;
      added = sig.connect([&] noexcept { order.push_back(2); });
    }
  })};
  (void)first;

  sig.emit();
  // Only the first slot fired; the deferred slot was not visited.
  REQUIRE(order.size() == 1);
  CHECK(order[0] == 1);
  CHECK(sig.size() == 2);  // deferred slot is now merged in

  order.clear();
  sig.emit();  // now both are live; first runs, then the once-deferred slot
  REQUIRE(order.size() == 2);
  CHECK(order[0] == 1);
  CHECK(order[1] == 2);
}

TEST_CASE("nexenne::signal::signal reentrancy (a-bulk): a slot connecting many slots forces no UAF"
) {
  // Connect enough new slots from inside the running callable to grow the inline
  // slot list past its inline capacity. Deferred, so the live list never moves.
  auto sig{signal<void()>{}};
  auto fired{0};
  auto keep{std::vector<connection>{}};
  auto first{sig.connect([&] noexcept {
    ++fired;
    for (auto i{0}; i < 20; ++i) {
      keep.push_back(sig.connect([&] noexcept { ++fired; }));
    }
  })};
  (void)first;

  sig.emit();  // only the first slot fires; the 20 deferred ones are not visited
  CHECK(fired == 1);
  CHECK(sig.size() == 21);
  sig.emit();  // now all 21 fire
  CHECK(fired == 1 + 21);
}

TEST_CASE("nexenne::signal::signal reentrancy (b): a slot can disconnect itself") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto conn{connection{}};
  conn = sig.connect([&] noexcept {
    ++count;
    conn.disconnect();
  });

  sig.emit();
  CHECK(count == 1);
  CHECK(sig.empty());

  sig.emit();
  CHECK(count == 1);
}

TEST_CASE(
  "nexenne::signal::signal reentrancy (c): disconnecting another not-yet-fired slot skips it"
) {
  auto sig{signal<void()>{}};
  auto log{std::vector<int>{}};
  auto c1{connection{}};
  auto c2{connection{}};
  auto c3{connection{}};

  c1 = sig.connect([&] noexcept { log.push_back(1); });
  c2 = sig.connect([&] noexcept {
    log.push_back(2);
    c3.disconnect();  // c3 has not fired yet: marked dead before the loop reaches it
  });
  c3 = sig.connect([&] noexcept { log.push_back(3); });

  sig.emit();
  REQUIRE(log.size() == 2);
  CHECK(log[0] == 1);
  CHECK(log[1] == 2);

  log.clear();
  sig.emit();
  REQUIRE(log.size() == 2);  // c3 is gone for good
  CHECK(log[0] == 1);
  CHECK(log[1] == 2);

  (void)c1;
}

TEST_CASE("nexenne::signal::signal reentrancy (d): disconnecting an already-fired slot is safe") {
  auto sig{signal<void()>{}};
  auto log{std::vector<int>{}};
  auto c1{connection{}};
  auto c2{connection{}};
  // Disconnect c1 exactly once (the slot body re-runs on every emit, and a
  // second disconnect of the same connection returns false by contract).
  auto disconnected_once{false};
  auto disconnect_result{false};

  c1 = sig.connect([&] noexcept { log.push_back(1); });
  c2 = sig.connect([&] noexcept {
    log.push_back(2);
    if (!disconnected_once) {
      disconnected_once = true;
      disconnect_result = c1.disconnect();  // c1 already fired this emit
    }
  });

  sig.emit();
  CHECK(disconnect_result);  // disconnecting an already-fired live slot succeeds
  REQUIRE(log.size() == 2);
  CHECK(log[0] == 1);
  CHECK(log[1] == 2);
  CHECK(sig.size() == 1);  // c1 swept after the emit

  log.clear();
  sig.emit();
  REQUIRE(log.size() == 1);  // only c2 remains
  CHECK(log[0] == 2);

  (void)c2;
}

TEST_CASE("nexenne::signal::signal reentrancy (e): disconnect_all during emit empties the signal") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto tail_fired{0};
  auto first{connection{}};
  auto tail{connection{}};

  first = sig.connect([&] noexcept {
    ++fired;
    sig.disconnect_all();  // documented @post: empty() afterward
  });
  tail = sig.connect([&] noexcept { ++tail_fired; });

  sig.emit();
  CHECK(fired == 1);
  CHECK(tail_fired == 0);  // tail was marked dead by disconnect_all before it fired
  CHECK(sig.empty());      // @post

  sig.emit();
  CHECK(fired == 1);
  CHECK(tail_fired == 0);

  (void)first;
  (void)tail;
}

TEST_CASE("nexenne::signal::signal reentrancy (e2): disconnect_all during emit drops a slot "
          "connected in the same emit") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto first{sig.connect([&] noexcept {
    ++fired;
    auto const pending{sig.connect([&] noexcept { ++fired; })};  // deferred this emit
    static_cast<void>(pending);
    sig.disconnect_all();  // must drop the deferred connect too, not just live slots
  })};
  (void)first;

  sig.emit();  // first fires, connects a slot, then disconnect_all
  sig.emit();  // nothing must fire: every slot, including the deferred one, is gone
  CHECK(fired == 1);
  CHECK(sig.empty());
}

TEST_CASE("nexenne::signal::signal reentrancy (f): nested emit of the same signal") {
  // A slot re-emits the same signal. The inner emit visits the slots alive at
  // entry; guard the recursion so it bottoms out, and assert the call counts.
  auto sig{signal<void()>{}};
  auto depth{0};
  auto outer_hits{0};
  auto inner_hits{0};

  auto re_emitter{sig.connect([&] noexcept {
    ++depth;
    if (depth == 1) {
      ++outer_hits;
      sig.emit();  // nested emit from within a slot
    } else {
      ++inner_hits;
    }
    --depth;
  })};
  auto plain{sig.connect([&] noexcept { /* present in both passes */ })};

  sig.emit();
  // Outer emit: re_emitter fires (depth 1) and triggers an inner emit.
  // Inner emit: re_emitter fires again (depth 2, inner_hits) and plain fires.
  CHECK(outer_hits == 1);
  CHECK(inner_hits == 1);
  CHECK(sig.size() == 2);  // both slots survive the nested emit

  (void)re_emitter;
  (void)plain;
}

TEST_CASE("nexenne::signal::signal reentrancy: disconnecting a slot connected in the same emit "
          "before it merges") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto first{sig.connect([&] noexcept {
    ++fired;
    auto pending{sig.connect([&] noexcept { ++fired; })};
    pending.disconnect();  // kill it before it is ever merged into the live list
  })};
  (void)first;

  sig.emit();  // first fires; the pending connection is marked dead, never merged
  sig.emit();  // so the second emit only fires the first slot again
  CHECK(fired == 2);
  CHECK(sig.size() == 1);
}

TEST_CASE("nexenne::signal::signal reentrancy: a slot may destroy the signal mid-emit") {
  auto sig{std::make_unique<signal<void()>>()};
  auto fired{0};
  auto c{sig->connect([&] noexcept {
    ++fired;
    sig.reset();  // destroy the signal from within its own emit
  })};
  (void)c;

  sig->emit();  // the pinned core keeps the iteration alive; must not crash
  CHECK(fired == 1);
  CHECK(sig == nullptr);
}

TEST_CASE("nexenne::signal::signal reentrancy: slot destroys signal, later slots still fire on "
          "the pinned core, connection sees null owner") {
  auto sig{std::make_unique<signal<void()>>()};
  auto first_fired{0};
  auto second_fired{0};

  auto c1{sig->connect([&] noexcept {
    ++first_fired;
    sig.reset();  // destroy mid-emit, before the second slot is visited
  })};
  auto c2{sig->connect([&] noexcept { ++second_fired; })};
  auto* const raw{sig.get()};

  raw->emit();
  CHECK(first_fired == 1);
  // The second slot still runs from the pinned core: it was alive at entry and
  // the in-flight emit completes on the immutable snapshot (a stronger guarantee
  // than Boost.Signals2, where destroying mid-emit is undefined).
  CHECK(second_fired == 1);
  CHECK(sig == nullptr);
  // Outstanding connections now see a null owner: disconnect is a no-op.
  CHECK_FALSE(c1.disconnect());
  CHECK_FALSE(c2.disconnect());
}

// lifetime tracking via slot

TEST_CASE("nexenne::signal::slot auto-disconnects every tracked slot on destruction") {
  auto sig{signal<void()>{}};
  auto fired{0};
  {
    auto owner{nexenne::signal::slot<4>{}};
    [[maybe_unused]] auto const c1{sig.connect([&fired] noexcept { ++fired; }, owner)};
    [[maybe_unused]] auto const c2{sig.connect([&fired] noexcept { ++fired; }, owner)};
    CHECK(owner.size() == 2);
    sig.emit();
    CHECK(fired == 2);
  }
  sig.emit();
  CHECK(fired == 2);  // owner dead -> no more slot invocations
  CHECK(sig.size() == 0);
}

TEST_CASE("nexenne::signal::slot.clear disconnects but stays usable") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto owner{nexenne::signal::slot<2>{}};
  [[maybe_unused]] auto const c1{sig.connect([&fired] noexcept { ++fired; }, owner)};
  sig.emit();
  CHECK(fired == 1);
  owner.clear();
  CHECK(owner.empty());
  sig.emit();
  CHECK(fired == 1);  // slot detached
  [[maybe_unused]] auto const c2{sig.connect([&fired] noexcept { ++fired; }, owner)};
  sig.emit();
  CHECK(fired == 2);  // owner is back in use
}

TEST_CASE("nexenne::signal::slot reports full when over capacity") {
  auto sig{signal<void()>{}};
  auto owner{nexenne::signal::slot<2>{}};
  [[maybe_unused]] auto const c1{sig.connect([] noexcept {}, owner)};
  [[maybe_unused]] auto const c2{sig.connect([] noexcept {}, owner)};
  CHECK(owner.full());
  CHECK(owner.size() == 2);
  CHECK(owner.capacity() == 2);
  [[maybe_unused]] auto const c3{sig.connect([] noexcept {}, owner)};
  CHECK(owner.size() == 2);  // overflow ignored, no heap
}

TEST_CASE("nexenne::signal::slot.track leaves the connection live and connected when full") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto tracker{nexenne::signal::slot<1>{}};

  auto c1{sig.connect([&] noexcept { ++count; })};
  CHECK(tracker.track(std::move(c1)));  // room for one, tracked

  auto c2{sig.connect([&] noexcept { ++count; })};
  CHECK_FALSE(tracker.track(c2));  // full, must report false WITHOUT disconnecting
  CHECK(c2.valid());

  sig.emit();
  CHECK(count == 2);  // both slots still fire: c2 was not disconnected by track
}

TEST_CASE("nexenne::signal::slot move transfers ownership of tracked connections") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto dst{nexenne::signal::slot<2>{}};
  {
    auto src{nexenne::signal::slot<2>{}};
    [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; }, src)};
    CHECK(src.size() == 1);
    dst = std::move(src);  // ownership of the subscription moves to dst
  }  // src dies, but it no longer owns the connection
  CHECK(dst.size() == 1);
  sig.emit();
  CHECK(fired == 1);  // still connected because dst keeps it alive
}

// weak_ptr/shared_ptr owner lifetime tracking

TEST_CASE("nexenne::signal::signal slot fires while a tracked owner is alive, stops once it dies") {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto owner{std::make_shared<int>(0)};
  auto weak{std::weak_ptr<int>{owner}};

  auto conn{sig.connect([weak, &fired] noexcept {
    if (auto const pinned{weak.lock()}) {
      ++fired;
    }
  })};

  sig.emit();
  CHECK(fired == 1);  // owner alive

  owner.reset();  // owner dies BETWEEN emits
  sig.emit();
  CHECK(fired == 1);  // slot saw the dead owner and did nothing

  (void)conn;
}

TEST_CASE("nexenne::signal::signal a tracked owner dying DURING an emit is observed by later slots"
) {
  auto sig{signal<void()>{}};
  auto owner{std::make_shared<int>(0)};
  auto weak{std::weak_ptr<int>{owner}};
  auto killer_ran{false};
  auto observer_saw_alive{false};

  // Killer fires first and destroys the owner mid-emit.
  auto killer{sig.connect(
    [&owner, &killer_ran] noexcept {
      killer_ran = true;
      owner.reset();
    },
    -1
  )};
  // Observer fires after and must see the owner already gone.
  auto observer{sig.connect(
    [weak, &observer_saw_alive] noexcept { observer_saw_alive = static_cast<bool>(weak.lock()); }, 0
  )};

  sig.emit();
  CHECK(killer_ran);
  CHECK_FALSE(observer_saw_alive);  // owner died during this same emit

  (void)killer;
  (void)observer;
}

// sink (connect-only view)

TEST_CASE("nexenne::signal::sink exposes connect but hides emit") {
  auto sig{signal<void(int)>{}};
  auto sink{sig.as_sink()};

  auto count{0};
  auto conn{sink.connect([&](int) noexcept { ++count; })};
  CHECK(sink.size() == 1);
  CHECK_FALSE(sink.empty());

  sig.emit(42);  // only the signal can fire
  CHECK(count == 1);

  (void)conn;
}

TEST_CASE("nexenne::signal::sink refers to the live signal: connects land on it") {
  auto sig{signal<void()>{}};
  auto a{sig.as_sink()};
  auto fired{0};
  // Connect via the signal and via the sink; both land on the same core.
  auto c1{sig.connect([&] noexcept { ++fired; })};
  auto c2{a.connect([&] noexcept { ++fired; })};
  CHECK(a.size() == 2);
  CHECK(sig.size() == 2);
  sig.emit();
  CHECK(fired == 2);
  (void)c1;
  (void)c2;
}

TEST_CASE("nexenne::signal::sink supports connect_once") {
  auto sig{signal<void()>{}};
  auto s{sig.as_sink()};
  auto fired{0};
  auto c{s.connect_once([&] noexcept { ++fired; })};
  sig.emit();
  sig.emit();
  CHECK(fired == 1);
  CHECK(sig.empty());
  (void)c;
}

// emit_blocker

TEST_CASE("nexenne::signal::signal block / unblock suppresses emit without disconnecting") {
  auto sig{signal<void()>{}};
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};
  sig.block();
  CHECK(sig.is_blocked());
  sig.emit();
  sig.emit();
  CHECK(fired == 0);
  CHECK(sig.size() == 1);  // slot stays connected while blocked
  sig.unblock();
  CHECK_FALSE(sig.is_blocked());
  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("nexenne::signal::emit_blocker scopes a block to a region") {
  auto sig{signal<void()>{}};
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};
  {
    nexenne::signal::emit_blocker guard{sig};
    CHECK(sig.is_blocked());
    sig.emit();
    sig.emit();
    sig.emit();
  }
  CHECK_FALSE(sig.is_blocked());  // restored on scope exit
  CHECK(fired == 0);
  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("nexenne::signal::emit_blocker.release ends the block early") {
  auto sig{signal<void()>{}};
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};
  nexenne::signal::emit_blocker guard{sig};
  sig.emit();
  CHECK(fired == 0);
  guard.release();
  CHECK_FALSE(sig.is_blocked());
  sig.emit();
  CHECK(fired == 1);
  guard.release();  // idempotent: second release is a no-op
  CHECK_FALSE(sig.is_blocked());
}

TEST_CASE("nexenne::signal::emit_blocker nested restores prior block, not unblocked baseline") {
  auto sig{signal<void()>{}};
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};

  nexenne::signal::emit_blocker outer{sig};
  {
    nexenne::signal::emit_blocker inner{sig};
    sig.emit();
    CHECK(fired == 0);
  }  // ~inner restores prior (which was blocked by outer)
  CHECK(sig.is_blocked());
  sig.emit();
  CHECK(fired == 0);  // outer still blocking
}

TEST_CASE("nexenne::signal::emit_blocker move transfers the restore (only the destination restores)"
) {
  auto sig{signal<void()>{}};
  auto fired{0};
  auto c{sig.connect([&fired] noexcept { ++fired; })};

  auto holder{std::optional<nexenne::signal::emit_blocker<signal<void()>>>{}};
  {
    nexenne::signal::emit_blocker inner{sig};
    CHECK(sig.is_blocked());
    holder.emplace(std::move(inner));  // move the restore out
  }  // ~inner is inert, must NOT restore
  CHECK(sig.is_blocked());  // still blocked: the moved-from guard did nothing
  holder.reset();           // destination restores here
  CHECK_FALSE(sig.is_blocked());
  sig.emit();
  CHECK(fired == 1);
  (void)c;
}

TEST_CASE("nexenne::signal::emit_blocker move-assign restores the prior signal first") {
  auto sig_a{signal<void()>{}};
  auto sig_b{signal<void()>{}};
  sig_a.block();  // pre-existing block: the blocker records this as a's prior state

  auto blocker_a{nexenne::signal::emit_blocker{sig_a}};  // a: prior=true, now blocked
  {
    auto blocker_b{nexenne::signal::emit_blocker{sig_b}};  // b: prior=false, now blocked
    CHECK(sig_b.is_blocked());
    blocker_a = std::move(blocker_b);  // restores a to its prior (true), adopts b
  }
  CHECK(sig_a.is_blocked());  // a restored to prior true by the move-assign
  CHECK(sig_b.is_blocked());  // b still held by blocker_a
}

TEST_CASE("nexenne::signal::blockable concept is satisfied by signal") {
  static_assert(nexenne::signal::blockable<signal<void()>>);
  static_assert(nexenne::signal::blockable<signal<int(int, double)>>);
  CHECK(true);
}

TEST_CASE("nexenne::signal::signal block before first connect still takes effect") {
  auto sig{signal<void()>{}};
  sig.block();  // pre-create core before any connect
  CHECK(sig.is_blocked());
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};
  sig.emit();
  CHECK(fired == 0);  // still blocked across the connect
  sig.unblock();
  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("nexenne::signal::signal unblock on a never-used signal is a safe no-op") {
  auto sig{signal<void()>{}};
  sig.unblock();  // core never created: must not crash
  CHECK_FALSE(sig.is_blocked());
}

// member-function connect

namespace {
struct receiver {
  int hits{0};

  auto on_event(int const v) noexcept -> void {
    hits += v;
  }
};
}  // namespace

TEST_CASE("nexenne::signal::signal member-function connect shortcut wires a method as a slot") {
  auto sig{signal<void(int)>{}};
  receiver r{};
  [[maybe_unused]] auto const c{sig.connect<&receiver::on_event>(r)};
  sig.emit(3);
  sig.emit(4);
  CHECK(r.hits == 7);
}

TEST_CASE("nexenne::signal::signal member-function connect with slot ties lifetime") {
  auto sig{signal<void(int)>{}};
  receiver r{};
  {
    auto owner{nexenne::signal::slot<2>{}};
    [[maybe_unused]] auto const c{sig.connect<&receiver::on_event>(r, owner)};
    sig.emit(5);
    CHECK(r.hits == 5);
  }
  sig.emit(10);
  CHECK(r.hits == 5);  // owner dropped -> no fire
}

TEST_CASE("nexenne::signal::sink supports slot and member-function overloads") {
  auto sig{signal<void(int)>{}};
  auto s{sig.as_sink()};
  receiver r{};
  {
    auto owner{nexenne::signal::slot<2>{}};
    [[maybe_unused]] auto const c{s.connect<&receiver::on_event>(r, owner)};
    sig.emit(7);
    CHECK(r.hits == 7);
  }
  sig.emit(11);
  CHECK(r.hits == 7);  // owner dropped -> no fire via sink-registered slot
}

// free-function fast path

namespace {
auto free_count{0};

auto free_increment(int n) noexcept -> void {
  free_count += n;
}
}  // namespace

TEST_CASE("nexenne::signal::signal accepts a free function (fast path)") {
  free_count = 0;
  auto sig{signal<void(int)>{}};
  auto conn{sig.connect(&free_increment)};

  sig.emit(5);
  sig.emit(10);
  CHECK(free_count == 15);
  (void)conn;
}

TEST_CASE("nexenne::signal::signal accepts a captureless lambda (fast path)") {
  free_count = 0;
  auto sig{signal<void(int)>{}};
  auto conn{sig.connect(+[](int n) noexcept { free_count += n; })};

  sig.emit(7);
  CHECK(free_count == 7);
  (void)conn;
}

TEST_CASE("nexenne::signal::signal mixes fast-path and capturing slots correctly") {
  free_count = 0;
  auto local_count{0};
  auto sig{signal<void(int)>{}};

  auto c1{sig.connect(&free_increment)};                                // fast
  auto c2{sig.connect([&](int n) noexcept { local_count += n * 2; })};  // slow (capture)

  sig.emit(3);
  CHECK(free_count == 3);
  CHECK(local_count == 6);

  (void)c1;
  (void)c2;
}

// connection identity

TEST_CASE("nexenne::signal::connection default is invalid") {
  auto const c{connection{}};
  CHECK_FALSE(c.valid());
}

TEST_CASE("nexenne::signal::connection equality: same slot, same signal") {
  auto sig{signal<void()>{}};
  auto c1{sig.connect([] noexcept {})};
  auto c2{c1};  // copy
  CHECK(c1 == c2);
  CHECK(c1.slot_id() == c2.slot_id());

  auto c3{sig.connect([] noexcept {})};
  CHECK_FALSE(c1 == c3);  // different slot

  (void)c1;
  (void)c3;
}

TEST_CASE("nexenne::signal::connection equality: different signals never compare equal") {
  auto a{signal<void()>{}};
  auto b{signal<void()>{}};
  auto ca{a.connect([] noexcept {})};
  auto cb{b.connect([] noexcept {})};
  // Even if both have slot_id 1, they refer to different cores.
  CHECK_FALSE(ca == cb);
}

TEST_CASE("nexenne::signal::signal survives destroy-during-emit (slot pulls the rug)") {
  auto sig{std::make_unique<signal<void()>>()};
  auto count{0};
  auto* sig_ptr{sig.get()};

  auto conn{sig->connect([&] noexcept {
    ++count;
    // Reset the unique_ptr from inside the slot - signal is now
    // being destroyed while we're still inside its emit().
    sig.reset();
  })};

  // We have to hold a raw pointer because we just deleted the owner.
  sig_ptr->emit();
  CHECK(count == 1);
  // No UB; the core was pinned during emit and outlived the signal.
  (void)conn;
}

}  // namespace
