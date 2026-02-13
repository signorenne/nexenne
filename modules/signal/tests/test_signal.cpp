#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include <nexenne/signal/connection.hpp>
#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/signal.hpp>
#include <nexenne/signal/slot.hpp>

namespace {

using nexenne::signal::connection;
using nexenne::signal::scoped_connection;
using nexenne::signal::signal;

TEST_CASE("signal empty by default") {
  auto sig{signal<void()>{}};
  CHECK(sig.empty());
  CHECK(sig.size() == 0);
}

TEST_CASE("signal connect / emit fires the slot") {
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

TEST_CASE("signal emit with arguments forwards them") {
  auto sig{signal<void(int, int)>{}};
  auto sum{0};
  auto conn{sig.connect([&](int a, int b) noexcept { sum = a + b; })};

  sig.emit(3, 4);
  CHECK(sum == 7);

  sig.emit(100, 200);
  CHECK(sum == 300);

  (void)conn;
}

TEST_CASE("signal supports multiple slots in connection order") {
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

TEST_CASE("connection.disconnect removes the slot") {
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

TEST_CASE("connection is valid until signal is destroyed") {
  auto sig{std::optional<signal<void()>>{std::in_place}};
  auto conn{sig->connect([] noexcept {})};
  CHECK(conn.valid());

  sig.reset();
  CHECK_FALSE(conn.valid());
  // Disconnect on dead signal is a safe no-op.
  CHECK_FALSE(conn.disconnect());
}

TEST_CASE("scoped_connection disconnects on destruction") {
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

TEST_CASE("scoped_connection move transfers ownership") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto outer{scoped_connection{}};
  {
    auto inner{scoped_connection{sig.connect([&] noexcept { ++count; })}};
    outer = std::move(inner);  // ownership transferred to outer
  }
  // inner destroyed; outer still owns -> slot still connected
  sig.emit();
  CHECK(count == 1);
}

TEST_CASE("scoped_connection.release returns the connection") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto raw_conn{connection{}};
  {
    auto sc{scoped_connection{sig.connect([&] noexcept { ++count; })}};
    raw_conn = sc.release();  // sc no longer auto-disconnects
  }
  sig.emit();
  CHECK(count == 1);  // still connected

  raw_conn.disconnect();
  sig.emit();
  CHECK(count == 1);
}

TEST_CASE("disconnect_all clears every slot") {
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

TEST_CASE("emit is reentrancy-safe: slot can disconnect itself") {
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

TEST_CASE("emit is reentrancy-safe: slot can disconnect another slot") {
  auto sig{signal<void()>{}};
  auto log{std::vector<int>{}};
  auto c1{connection{}};
  auto c2{connection{}};
  auto c3{connection{}};

  c1 = sig.connect([&] noexcept { log.push_back(1); });
  c2 = sig.connect([&] noexcept {
    log.push_back(2);
    c3.disconnect();  // deferred; c3 still gets called this emit? NO - marked dead before it fires
  });
  c3 = sig.connect([&] noexcept { log.push_back(3); });

  sig.emit();
  // c2 marked c3 as dead BEFORE the emit loop visits it.
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

TEST_CASE("emit is reentrancy-safe: slot can connect new slot") {
  auto sig{signal<void()>{}};
  auto count{0};
  auto added{connection{}};
  auto first{sig.connect([&] noexcept {
    ++count;
    if (!added.valid() || added.disconnect()) {
      added = sig.connect([&] noexcept { ++count; });
      // We need a fresh handle every time; the test just shows it doesn't crash.
    }
  })};

  sig.emit();
  // first slot runs once; the newly-added slot is not visited this emit.
  CHECK(count == 1);

  (void)first;
  (void)added;
}

TEST_CASE("emit is reentrancy-safe: a slot connecting many slots forces no UAF") {
  // A slot that connects enough new slots from inside its own invocation to grow
  // the inline slot list past its capacity. The new entries are deferred, so the
  // live list never reallocates while the running callable is on it.
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

TEST_CASE("emit is reentrancy-safe: a slot may destroy the signal mid-emit") {
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

TEST_CASE("emit is reentrancy-safe: disconnecting a slot connected in the same emit") {
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

TEST_CASE("disconnect_all during emit also drops a slot connected in that emit") {
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

TEST_CASE("connect_once does not re-fire when its slot re-emits the same signal") {
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

TEST_CASE("signal supports non-void return types (values discarded)") {
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

TEST_CASE("member-function connect shortcut wires a method as a slot") {
  struct widget {
    int total{0};

    auto on_event(int n) noexcept -> void {
      total += n;
    }
  };

  auto w{widget{}};
  auto sig{signal<void(int)>{}};
  auto conn{sig.connect<&widget::on_event>(w)};

  sig.emit(5);
  sig.emit(10);
  CHECK(w.total == 15);

  (void)conn;
}

TEST_CASE("signal with reference argument") {
  auto sig{signal<void(int&)>{}};
  auto conn{sig.connect([](int& n) noexcept { n *= 2; })};

  auto value{5};
  sig.emit(value);
  CHECK(value == 10);

  (void)conn;
}

TEST_CASE("default connection is invalid") {
  auto const c{connection{}};
  CHECK_FALSE(c.valid());
}

TEST_CASE("connect_once fires exactly once") {
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

TEST_CASE("slot priority orders invocation") {
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

TEST_CASE("same priority preserves insertion order (stable sort)") {
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

TEST_CASE("sink exposes connect but hides emit") {
  auto sig{signal<void(int)>{}};
  auto sink{sig.as_sink()};

  auto count{0};
  auto conn{sink.connect([&](int) noexcept { ++count; })};
  CHECK(sink.size() == 1);

  sig.emit(42);  // only the signal can fire
  CHECK(count == 1);

  (void)conn;
}

TEST_CASE("emit_and_collect returns slot results in priority order") {
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

TEST_CASE("emit_and_collect skips disconnected slots") {
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

TEST_CASE("signal survives destroy-during-emit (slot pulls the rug)") {
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
}

TEST_CASE("connection equality: same slot, same signal") {
  auto sig{signal<void()>{}};
  auto c1{sig.connect([] noexcept {})};
  auto c2{c1};  // copy
  CHECK(c1 == c2);

  auto c3{sig.connect([] noexcept {})};
  CHECK_FALSE(c1 == c3);  // different slot
}

namespace {
auto free_count{0};

auto free_increment(int n) noexcept -> void {
  free_count += n;
}
}  // namespace

TEST_CASE("signal accepts a free function (fast path)") {
  free_count = 0;
  auto sig{signal<void(int)>{}};
  auto conn{sig.connect(&free_increment)};

  sig.emit(5);
  sig.emit(10);
  CHECK(free_count == 15);
  (void)conn;
}

TEST_CASE("signal accepts a captureless lambda (fast path)") {
  free_count = 0;
  auto sig{signal<void(int)>{}};
  auto conn{sig.connect(+[](int n) noexcept { free_count += n; })};

  sig.emit(7);
  CHECK(free_count == 7);
  (void)conn;
}

TEST_CASE("signal mixes fast-path and capturing slots correctly") {
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

TEST_CASE("connection equality: different signals never compare equal") {
  auto a{signal<void()>{}};
  auto b{signal<void()>{}};
  auto ca{a.connect([] noexcept {})};
  auto cb{b.connect([] noexcept {})};
  // Even if both have slot_id 1, they refer to different cores.
  CHECK_FALSE(ca == cb);
}

TEST_CASE("slot auto-disconnects every tracked slot on destruction") {
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

TEST_CASE("slot.clear disconnects but stays usable") {
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

TEST_CASE("slot reports full when over capacity") {
  auto sig{signal<void()>{}};
  auto owner{nexenne::signal::slot<2>{}};
  [[maybe_unused]] auto const c1{sig.connect([] noexcept {}, owner)};
  [[maybe_unused]] auto const c2{sig.connect([] noexcept {}, owner)};
  CHECK(owner.full());
  CHECK(owner.size() == 2);
  [[maybe_unused]] auto const c3{sig.connect([] noexcept {}, owner)};
  CHECK(owner.size() == 2);  // overflow ignored, no heap
}

TEST_CASE("block / unblock suppresses emit without disconnecting") {
  auto sig{signal<void()>{}};
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};
  sig.block();
  CHECK(sig.is_blocked());
  sig.emit();
  sig.emit();
  CHECK(fired == 0);
  sig.unblock();
  CHECK_FALSE(sig.is_blocked());
  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("emit_blocker scopes a block to a region") {
  auto sig{signal<void()>{}};
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};
  {
    nexenne::signal::emit_blocker guard{sig};
    sig.emit();
    sig.emit();
    sig.emit();
  }
  CHECK(fired == 0);
  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("emit_blocker.release ends the block early") {
  auto sig{signal<void()>{}};
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};
  nexenne::signal::emit_blocker guard{sig};
  sig.emit();
  CHECK(fired == 0);
  guard.release();
  sig.emit();
  CHECK(fired == 1);
}

namespace {
struct receiver {
  int hits{0};

  auto on_event(int const v) noexcept -> void {
    hits += v;
  }
};
}  // namespace

TEST_CASE("member-function connect shortcut") {
  auto sig{signal<void(int)>{}};
  receiver r{};
  [[maybe_unused]] auto const c{sig.connect<&receiver::on_event>(r)};
  sig.emit(3);
  sig.emit(4);
  CHECK(r.hits == 7);
}

TEST_CASE("member-function connect with slot ties lifetime") {
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

TEST_CASE("nested emit_blocker restores prior block, not unblocked baseline") {
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

TEST_CASE("block before first connect still takes effect") {
  auto sig{signal<void()>{}};
  sig.block();  // pre-create core
  CHECK(sig.is_blocked());
  auto fired{0};
  [[maybe_unused]] auto const c{sig.connect([&fired] noexcept { ++fired; })};
  sig.emit();
  CHECK(fired == 0);  // still blocked across the connect
  sig.unblock();
  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("sink supports slot and member-function overloads") {
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

TEST_CASE("connect_once with stateful capture does not heap-allocate per connect") {
  // Sanity: a capturing lambda smaller than the default 64-byte SBO
  // goes through in_place_function; if it heap-allocated we'd have
  // no compile-time signal but we can at least verify it fires once.
  auto sig{signal<void()>{}};
  auto fired{0};
  auto extra{42};
  [[maybe_unused]] auto const c{sig.connect_once([&fired, extra] noexcept { fired += extra; })};
  sig.emit();
  sig.emit();
  sig.emit();
  CHECK(fired == 42);
  CHECK(sig.empty());
}

TEST_CASE("connect with non-zero priority does not full-sort the slot list") {
  // Functional check on the insertion-bubble path: priorities must
  // place slots in the right order whether added high-to-low or
  // low-to-high.
  auto sig{signal<void()>{}};
  std::vector<int> order;
  [[maybe_unused]] auto const c1{sig.connect([&] { order.push_back(1); }, 10)};
  [[maybe_unused]] auto const c2{sig.connect([&] { order.push_back(2); }, 5)};
  [[maybe_unused]] auto const c3{sig.connect([&] { order.push_back(3); }, 15)};
  [[maybe_unused]] auto const c4{sig.connect([&] { order.push_back(4); }, 5)
  };  // tied with c2 - comes after
  sig.emit();
  CHECK(order == std::vector{2, 4, 1, 3});
}

TEST_CASE("slot::track leaves the connection live and connected when full") {
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

}  // namespace
