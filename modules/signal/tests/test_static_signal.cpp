#include <doctest/doctest.h>

#include <cstddef>
#include <new>
#include <utility>
#include <vector>

#include <nexenne/signal/emit_blocker.hpp>
#include <nexenne/signal/static_signal.hpp>

namespace {

using nexenne::signal::static_connection;
using nexenne::signal::static_scoped_connection;
using nexenne::signal::static_signal;
using nexenne::signal::static_slot;

struct alloc_counter {
  static inline std::size_t allocations{0};
  static inline std::size_t deallocations{0};

  static auto snapshot() noexcept -> std::size_t {
    return allocations;
  }
};

}  // namespace

auto operator new(std::size_t const size) -> void* {
  ++alloc_counter::allocations;
  // [new.delete] requires a non-null, distinct pointer even for a zero request;
  // round up to one byte so the underlying allocator always returns storage.
  auto* const p{__builtin_malloc(size == 0 ? std::size_t{1} : size)};
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

auto operator delete(void* const ptr) noexcept -> void {
  if (ptr != nullptr) {
    ++alloc_counter::deallocations;
    __builtin_free(ptr);
  }
}

auto operator delete(void* const ptr, std::size_t) noexcept -> void {
  ::operator delete(ptr);
}

namespace {

// Counts every move/copy of a value type, to prove emit forwards by const&
// (one copy per slot at the slot's own call boundary, not per forwarder).
struct copy_probe {
  static inline int copies{0};
  static inline int moves{0};

  int value{0};

  copy_probe() noexcept = default;

  explicit copy_probe(int const v) noexcept : value{v} {}

  copy_probe(copy_probe const& other) noexcept : value{other.value} {
    ++copies;
  }

  copy_probe(copy_probe&& other) noexcept : value{other.value} {
    ++moves;
  }

  auto operator=(copy_probe const&) -> copy_probe& = default;
  auto operator=(copy_probe&&) -> copy_probe& = default;
  ~copy_probe() = default;

  static auto reset() noexcept -> void {
    copies = 0;
    moves = 0;
  }
};

TEST_CASE("nexenne::signal::static_signal empty by default") {
  auto sig{static_signal<void()>{}};
  CHECK(sig.empty());
  CHECK(sig.size() == 0);
  CHECK_FALSE(sig.full());
  CHECK(sig.capacity() == 8);  // default MaxSlots
  CHECK_FALSE(sig.is_blocked());
}

TEST_CASE("nexenne::signal::static_signal connect then emit fires the slot") {
  auto sig{static_signal<void()>{}};
  auto count{0};
  auto const conn{sig.connect([&] noexcept { ++count; })};
  CHECK(conn.has_target());
  CHECK(sig.size() == 1);

  sig.emit();
  CHECK(count == 1);
  sig.emit();
  CHECK(count == 2);
}

TEST_CASE("nexenne::signal::static_signal operator() aliases emit") {
  auto sig{static_signal<void(int)>{}};
  auto got{0};
  auto const conn{sig.connect([&](int v) noexcept { got = v; })};
  static_cast<void>(conn);

  sig(11);
  CHECK(got == 11);
}

TEST_CASE("nexenne::signal::static_signal multiple slots fire in insertion order") {
  auto sig{static_signal<void(int)>{}};
  auto log{std::vector<int>{}};
  auto const c1{sig.connect([&](int n) noexcept { log.push_back(n * 1); })};
  auto const c2{sig.connect([&](int n) noexcept { log.push_back(n * 2); })};
  auto const c3{sig.connect([&](int n) noexcept { log.push_back(n * 3); })};
  static_cast<void>(c1);
  static_cast<void>(c2);
  static_cast<void>(c3);

  sig.emit(5);
  REQUIRE(log.size() == 3);
  CHECK(log[0] == 5);
  CHECK(log[1] == 10);
  CHECK(log[2] == 15);
}

TEST_CASE("nexenne::signal::static_signal forwards reference arguments without copying") {
  auto sig{static_signal<void(int&)>{}};
  auto const conn{sig.connect([](int& n) noexcept { n *= 2; })};
  static_cast<void>(conn);

  auto value{5};
  sig.emit(value);
  CHECK(value == 10);
}

TEST_CASE("nexenne::signal::static_signal copies a by-value arg once per slot") {
  auto sig{static_signal<void(copy_probe)>{}};
  auto sum{0};
  auto const c1{sig.connect([&](copy_probe p) noexcept { sum += p.value; })};
  auto const c2{sig.connect([&](copy_probe p) noexcept { sum += p.value; })};
  static_cast<void>(c1);
  static_cast<void>(c2);

  copy_probe::reset();
  auto const arg{copy_probe{7}};
  sig.emit(arg);
  CHECK(sum == 14);
  // emit takes the parameter by value (one copy from caller), then passes it by
  // const& to each slot, where each by-value slot parameter copies once: total
  // one outer copy + one per slot. The forwarder itself adds none.
  CHECK(copy_probe::copies == 1 + 2);
}

TEST_CASE("nexenne::signal::static_signal value-returning slots discard their results on emit") {
  auto sig{static_signal<int(int)>{}};
  auto last{0};
  auto const conn{sig.connect([&](int n) noexcept -> int {
    last = n;
    return n * 2;
  })};
  static_cast<void>(conn);

  sig.emit(7);
  CHECK(last == 7);  // emit returns void; the slot's int is discarded
}

TEST_CASE("nexenne::signal::static_signal disconnect removes the slot") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto conn{sig.connect([&] noexcept { ++n; })};

  sig.emit();
  CHECK(n == 1);
  CHECK(conn.disconnect());
  CHECK(sig.size() == 0);
  CHECK(sig.empty());

  sig.emit();
  CHECK(n == 1);                   // unchanged
  CHECK_FALSE(conn.disconnect());  // already disconnected
}

TEST_CASE("nexenne::signal::static_signal disconnect_all clears every slot") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto const c1{sig.connect([&] noexcept { ++n; })};
  auto const c2{sig.connect([&] noexcept { ++n; })};
  auto const c3{sig.connect([&] noexcept { ++n; })};
  static_cast<void>(c1);
  static_cast<void>(c2);
  static_cast<void>(c3);
  CHECK(sig.size() == 3);

  sig.disconnect_all();
  CHECK(sig.empty());
  sig.emit();
  CHECK(n == 0);
}

TEST_CASE("nexenne::signal::static_signal lower priority fires first") {
  auto sig{static_signal<void()>{}};
  auto log{std::vector<int>{}};
  auto const c_low{sig.connect([&] noexcept { log.push_back(2); }, 10)};
  auto const c_mid{sig.connect([&] noexcept { log.push_back(1); }, 0)};
  auto const c_high{sig.connect([&] noexcept { log.push_back(0); }, -5)};
  static_cast<void>(c_low);
  static_cast<void>(c_mid);
  static_cast<void>(c_high);

  sig.emit();
  REQUIRE(log.size() == 3);
  CHECK(log[0] == 0);  // -5
  CHECK(log[1] == 1);  // 0
  CHECK(log[2] == 2);  // 10
}

TEST_CASE("nexenne::signal::static_signal equal priority keeps insertion order (stable)") {
  auto sig{static_signal<void(), 8>{}};
  auto order{std::vector<int>{}};
  auto const c1{sig.connect([&] noexcept { order.push_back(1); }, 10)};
  auto const c2{sig.connect([&] noexcept { order.push_back(2); }, 5)};
  auto const c3{sig.connect([&] noexcept { order.push_back(3); }, 15)};
  auto const c4{sig.connect([&] noexcept { order.push_back(4); }, 5)};  // tied w/ c2
  static_cast<void>(c1);
  static_cast<void>(c2);
  static_cast<void>(c3);
  static_cast<void>(c4);

  sig.emit();
  CHECK(order == std::vector{2, 4, 1, 3});
}

TEST_CASE("nexenne::signal::static_signal connect succeeds up to MaxSlots") {
  auto sig{static_signal<void(), 4>{}};
  auto const c0{sig.connect([] noexcept {})};
  auto const c1{sig.connect([] noexcept {})};
  auto const c2{sig.connect([] noexcept {})};
  auto const c3{sig.connect([] noexcept {})};  // exactly MaxSlots
  CHECK(c0.has_target());
  CHECK(c1.has_target());
  CHECK(c2.has_target());
  CHECK(c3.has_target());
  CHECK(sig.size() == 4);
  CHECK(sig.full());
}

TEST_CASE("nexenne::signal::static_signal connect past MaxSlots fails without corruption") {
  auto sig{static_signal<void(int), 3>{}};
  auto log{std::vector<int>{}};
  auto const a{sig.connect([&](int v) noexcept { log.push_back(v + 1); })};
  auto const b{sig.connect([&](int v) noexcept { log.push_back(v + 2); })};
  auto const c{sig.connect([&](int v) noexcept { log.push_back(v + 3); })};
  CHECK(a.has_target());
  CHECK(b.has_target());
  CHECK(c.has_target());
  CHECK(sig.full());

  // MaxSlots + 1: must fail with an invalid handle, callable not stored.
  auto const d{sig.connect([&](int v) noexcept { log.push_back(v + 999); })};
  CHECK_FALSE(d.has_target());
  CHECK(d.id() == 0);
  CHECK(sig.size() == 3);  // existing slots untouched

  // The existing three still fire correctly; the rejected one never does.
  sig.emit(0);
  REQUIRE(log.size() == 3);
  CHECK(log[0] == 1);
  CHECK(log[1] == 2);
  CHECK(log[2] == 3);
}

TEST_CASE("nexenne::signal::static_signal disconnecting frees a slot for reuse") {
  auto sig{static_signal<void(), 2>{}};
  auto a{sig.connect([] noexcept {})};
  auto const b{sig.connect([] noexcept {})};
  static_cast<void>(b);
  CHECK(sig.full());

  auto const blocked{sig.connect([] noexcept {})};
  CHECK_FALSE(blocked.has_target());  // full

  CHECK(a.disconnect());
  CHECK_FALSE(sig.full());
  auto const reused{sig.connect([] noexcept {})};  // room again
  CHECK(reused.has_target());
  CHECK(sig.full());
  static_cast<void>(reused);
}

TEST_CASE("nexenne::signal::static_signal accepts a callable that fits SlotCapacity") {
  // SlotCapacity 32 bytes; a couple of ints captured by value fit comfortably.
  auto sig{static_signal<void(int), 4, 32>{}};
  auto sink{0};
  auto const a{1000};
  auto const b{7};
  auto const conn{sig.connect([a, b, &sink](int v) noexcept { sink = a + b + v; })};
  static_cast<void>(conn);

  sig.emit(1);
  CHECK(sink == 1008);
}

TEST_CASE("nexenne::signal::static_signal an exactly-fitting capturing lambda works") {
  // A lambda capturing a single pointer-sized reference fits in any reasonable
  // SlotCapacity; here we use a deliberately tight 16-byte budget to show a
  // small capturing closure is still inline.
  auto sig{static_signal<void(), 2, 16>{}};
  auto fired{0};
  auto const conn{sig.connect([&fired] noexcept { ++fired; })};
  static_cast<void>(conn);

  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("nexenne::signal::static_signal connect_once fires exactly once then is swept") {
  auto sig{static_signal<void(int)>{}};
  auto seen{std::vector<int>{}};
  auto const conn{sig.connect_once([&](int n) noexcept { seen.push_back(n); })};
  static_cast<void>(conn);

  sig.emit(1);
  sig.emit(2);
  sig.emit(3);
  REQUIRE(seen.size() == 1);
  CHECK(seen[0] == 1);
  CHECK(sig.empty());
}

TEST_CASE("nexenne::signal::static_signal once slot mixed with a persistent slot") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto once{0};
  auto const a{sig.connect([&] noexcept { ++n; })};
  auto const b{sig.connect_once([&] noexcept { ++once; })};
  static_cast<void>(a);
  static_cast<void>(b);

  sig.emit();
  sig.emit();
  CHECK(n == 2);
  CHECK(once == 1);
  CHECK(sig.size() == 1);
}

TEST_CASE("nexenne::signal::static_signal once slot re-emitting itself does not loop") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto const c{sig.connect_once([&] noexcept {
    ++n;
    if (n < 5) {
      sig.emit();  // a still-alive once slot would recurse without bound
    }
  })};
  static_cast<void>(c);

  sig.emit();
  CHECK(n == 1);  // marked dead before invoke, so the re-entrant emit skips it
  CHECK(sig.empty());
}

TEST_CASE("nexenne::signal::static_signal block / unblock suppresses emit") {
  auto sig{static_signal<void()>{}};
  auto fired{0};
  auto const c{sig.connect([&fired] noexcept { ++fired; })};
  static_cast<void>(c);

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

TEST_CASE("nexenne::signal::static_signal satisfies blockable and works with emit_blocker") {
  static_assert(nexenne::signal::blockable<static_signal<void()>>);

  auto sig{static_signal<void()>{}};
  auto fired{0};
  auto const c{sig.connect([&fired] noexcept { ++fired; })};
  static_cast<void>(c);
  {
    auto const guard{nexenne::signal::emit_blocker{sig}};
    sig.emit();
    sig.emit();
    CHECK(fired == 0);
  }
  CHECK_FALSE(sig.is_blocked());
  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("nexenne::signal::static_signal nested emit_blocker restores the prior block") {
  auto sig{static_signal<void()>{}};
  auto fired{0};
  auto const c{sig.connect([&fired] noexcept { ++fired; })};
  static_cast<void>(c);

  auto outer{nexenne::signal::emit_blocker{sig}};
  {
    auto const inner{nexenne::signal::emit_blocker{sig}};
    sig.emit();
    CHECK(fired == 0);
  }  // ~inner restores prior state (still blocked by outer)
  CHECK(sig.is_blocked());
  sig.emit();
  CHECK(fired == 0);
}

TEST_CASE("nexenne::signal::static_signal connect during emit appends past the live length") {
  auto sig{static_signal<void(), 8>{}};
  auto order{std::vector<int>{}};
  auto first{sig.connect([&] noexcept {
    order.push_back(1);
    // Connect a brand-new slot from inside the running emit. It must NOT fire
    // in this emit, but must be present afterwards.
    static_cast<void>(sig.connect([&] noexcept { order.push_back(99); }));
  })};
  static_cast<void>(first);

  sig.emit();  // only the first slot fires
  REQUIRE(order.size() == 1);
  CHECK(order[0] == 1);
  CHECK(sig.size() == 2);  // the new slot is present post-emit

  order.clear();
  sig.emit();  // now both fire (first re-adds another, deferred again)
  REQUIRE(order.size() == 2);
  CHECK(order[0] == 1);
  CHECK(order[1] == 99);
}

TEST_CASE("nexenne::signal::static_signal slot connected mid-emit is re-sorted at outermost end") {
  // The new slot has a LOWER priority than the running one, so once the
  // outermost emit re-sorts, the next emit visits it before the appender.
  auto sig{static_signal<void(), 8>{}};
  auto order{std::vector<int>{}};
  auto added{false};
  auto first{sig.connect(
    [&] noexcept {
      order.push_back(10);
      if (!added) {
        added = true;
        // priority -5: belongs BEFORE the priority-10 appender after re-sort.
        static_cast<void>(sig.connect([&] noexcept { order.push_back(-5); }, -5));
      }
    },
    10
  )};
  static_cast<void>(first);

  sig.emit();  // only the appender fires; new slot deferred
  REQUIRE(order.size() == 1);
  CHECK(order[0] == 10);

  order.clear();
  sig.emit();  // re-sorted: priority -5 slot now fires first
  REQUIRE(order.size() == 2);
  CHECK(order[0] == -5);
  CHECK(order[1] == 10);
}

TEST_CASE("nexenne::signal::static_signal slot can disconnect itself during emit") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto c{static_connection{}};
  c = sig.connect([&] noexcept {
    ++n;
    c.disconnect();  // deferred removal; swept after the outermost emit
  });

  sig.emit();
  CHECK(n == 1);
  CHECK(sig.empty());
  sig.emit();
  CHECK(n == 1);  // gone for good
}

TEST_CASE("nexenne::signal::static_signal slot disconnecting a not-yet-fired slot skips it") {
  auto sig{static_signal<void(), 8>{}};
  auto log{std::vector<int>{}};
  auto c1{static_connection{}};
  auto c2{static_connection{}};
  auto c3{static_connection{}};

  c1 = sig.connect([&] noexcept { log.push_back(1); });
  c2 = sig.connect([&] noexcept {
    log.push_back(2);
    c3.disconnect();  // c3 not yet visited this emit -> marked dead, skipped
  });
  c3 = sig.connect([&] noexcept { log.push_back(3); });

  sig.emit();
  REQUIRE(log.size() == 2);
  CHECK(log[0] == 1);
  CHECK(log[1] == 2);

  log.clear();
  sig.emit();  // c3 is gone permanently
  REQUIRE(log.size() == 2);
  CHECK(log[0] == 1);
  CHECK(log[1] == 2);
  CHECK(sig.size() == 2);
  static_cast<void>(c1);
}

TEST_CASE("nexenne::signal::static_signal nested emit is safe") {
  auto sig{static_signal<void(), 8>{}};
  auto outer{0};
  auto inner{0};
  auto reentered{false};
  auto first{sig.connect([&] noexcept {
    ++outer;
    if (!reentered) {
      reentered = true;
      sig.emit();  // nested emit of the same signal
    }
  })};
  auto second{sig.connect([&] noexcept { ++inner; })};
  static_cast<void>(first);
  static_cast<void>(second);

  sig.emit();
  // Outer emit visits both; the nested emit (from the first slot) also visits
  // both. first runs twice (outer entry + nested entry), second runs twice.
  CHECK(outer == 2);
  CHECK(inner == 2);
}

TEST_CASE("nexenne::signal::static_signal connecting many slots mid-emit forces no UAF") {
  // Fill nearly to capacity from inside a running slot. Because the slot list
  // is a fixed array that never reallocates, the deferred appends cannot move
  // the running callable. New slots are skipped this emit, present next emit.
  auto sig{static_signal<void(), 8>{}};
  auto fired{0};
  auto first{sig.connect([&] noexcept {
    ++fired;
    for (auto i{0}; i < 5; ++i) {
      static_cast<void>(sig.connect([&] noexcept { ++fired; }));
    }
  })};
  static_cast<void>(first);

  sig.emit();  // only first fires; the 5 deferred are not visited
  CHECK(fired == 1);
  CHECK(sig.size() == 6);
}

TEST_CASE("nexenne::signal::static_signal disconnect_all during emit defers and clears") {
  auto sig{static_signal<void(), 8>{}};
  auto fired{0};
  auto first{sig.connect([&] noexcept {
    ++fired;
    sig.disconnect_all();  // marks all dead; swept after the emit
  })};
  auto second{sig.connect([&] noexcept { ++fired; })};
  static_cast<void>(first);
  static_cast<void>(second);

  sig.emit();
  // first fires and marks every slot dead, so second (not yet visited) is
  // skipped; both are swept at the end.
  CHECK(fired == 1);
  CHECK(sig.empty());
  sig.emit();
  CHECK(fired == 1);
}

TEST_CASE("nexenne::signal::static_signal connect + emit + disconnect allocate nothing") {
  auto sig{static_signal<void(int), 8, 32>{}};
  auto total{0};

  auto const before{alloc_counter::snapshot()};

  // Connect several slots (capturing lambdas, inline storage).
  auto c0{sig.connect([&](int v) noexcept { total += v; })};
  auto c1{sig.connect([&](int v) noexcept { total += v * 2; }, -1)};
  auto c2{sig.connect_once([&](int v) noexcept { total += v * 3; })};

  // Emit a few times.
  sig.emit(1);
  sig.emit(1);
  sig.emit(1);

  // Disconnect.
  CHECK(c0.disconnect());
  CHECK(c1.disconnect());

  auto const after{alloc_counter::snapshot()};
  CHECK(after - before == 0);  // ZERO allocations across the whole sequence

  static_cast<void>(c2);
  static_cast<void>(total);
}

TEST_CASE("nexenne::signal::static_signal reentrant connect during emit allocates nothing") {
  auto sig{static_signal<void(), 8>{}};
  auto fired{0};
  auto first{sig.connect([&] noexcept {
    ++fired;
    if (sig.size() < 5) {
      static_cast<void>(sig.connect([&] noexcept { ++fired; }));
    }
  })};
  static_cast<void>(first);

  auto const before{alloc_counter::snapshot()};
  sig.emit();  // appends a slot mid-emit, then re-sorts at the end
  sig.emit();
  sig.emit();
  auto const after{alloc_counter::snapshot()};
  CHECK(after - before == 0);  // the fixed array never grows the heap
}

TEST_CASE("nexenne::signal::static_slot tracking allocates nothing") {
  auto sig{static_signal<void(), 8>{}};
  auto n{0};

  auto const before{alloc_counter::snapshot()};
  {
    auto tracker{static_slot<4>{}};
    static_cast<void>(sig.connect([&] noexcept { ++n; }, tracker));
    static_cast<void>(sig.connect([&] noexcept { ++n; }, tracker));
    sig.emit();
  }  // tracker destruction disconnects, still no heap
  sig.emit();
  auto const after{alloc_counter::snapshot()};

  CHECK(n == 2);
  CHECK(after - before == 0);
}

TEST_CASE("nexenne::signal::static_connection default is invalid") {
  auto const c{static_connection{}};
  CHECK_FALSE(c.has_target());
  CHECK(c.id() == 0);

  auto mutable_c{static_connection{}};
  CHECK_FALSE(mutable_c.disconnect());  // no-op on invalid
}

TEST_CASE("nexenne::signal::static_connection copies share one logical connection") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto c1{sig.connect([&] noexcept { ++n; })};
  auto c2{c1};  // copy refers to the same slot id
  CHECK(c1.id() == c2.id());

  CHECK(c1.disconnect());        // first wins
  CHECK_FALSE(c2.disconnect());  // already disconnected
  sig.emit();
  CHECK(n == 0);
}

TEST_CASE("nexenne::signal::static_connection ids are unique per connect") {
  auto sig{static_signal<void(), 4>{}};
  auto const a{sig.connect([] noexcept {})};
  auto const b{sig.connect([] noexcept {})};
  CHECK(a.id() != b.id());
  CHECK(a.has_target());
  CHECK(b.has_target());
}

TEST_CASE("nexenne::signal::static_scoped_connection disconnects on scope exit") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  {
    auto const sc{static_scoped_connection{sig.connect([&] noexcept { ++n; })}};
    sig.emit();
    CHECK(n == 1);
  }  // ~static_scoped_connection disconnects
  sig.emit();
  CHECK(n == 1);
  CHECK(sig.empty());
}

TEST_CASE("nexenne::signal::static_scoped_connection move transfers ownership") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto outer{static_scoped_connection{}};
  {
    auto inner{static_scoped_connection{sig.connect([&] noexcept { ++n; })}};
    outer = std::move(inner);  // ownership moves out; inner now empty
  }  // ~inner disconnects nothing
  sig.emit();
  CHECK(n == 1);  // outer still owns the live slot
}

TEST_CASE("nexenne::signal::static_scoped_connection move-construct keeps the slot live") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto first{static_scoped_connection{sig.connect([&] noexcept { ++n; })}};
  auto second{std::move(first)};  // move-construct
  sig.emit();
  CHECK(n == 1);  // moved-into object owns it
  static_cast<void>(second);
}

TEST_CASE("nexenne::signal::static_scoped_connection release disarms the auto-disconnect") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto raw{static_connection{}};
  {
    auto sc{static_scoped_connection{sig.connect([&] noexcept { ++n; })}};
    raw = sc.release();  // sc no longer disconnects on scope exit
  }
  sig.emit();
  CHECK(n == 1);  // still connected

  CHECK(raw.disconnect());
  sig.emit();
  CHECK(n == 1);
}

TEST_CASE("nexenne::signal::static_scoped_connection explicit disconnect") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto sc{static_scoped_connection{sig.connect([&] noexcept { ++n; })}};
  CHECK(sc.disconnect());
  CHECK_FALSE(sc.disconnect());  // idempotent
  sig.emit();
  CHECK(n == 0);
}

TEST_CASE("nexenne::signal::static_scoped_connection move-assign disconnects the prior slot") {
  auto sig{static_signal<void(), 4>{}};
  auto first{0};
  auto second{0};
  auto holder{static_scoped_connection{sig.connect([&] noexcept { ++first; })}};
  // Reassign: the first slot must be disconnected before adopting the second.
  holder = static_scoped_connection{sig.connect([&] noexcept { ++second; })};

  sig.emit();
  CHECK(first == 0);   // disconnected by the move-assign
  CHECK(second == 1);  // newly adopted slot fires
}

TEST_CASE("nexenne::signal::static_slot auto-disconnects all on destruction") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  {
    auto tracker{static_slot<2>{}};
    static_cast<void>(sig.connect([&] noexcept { ++n; }, tracker));
    static_cast<void>(sig.connect([&] noexcept { ++n; }, tracker));
    CHECK(tracker.size() == 2);
    CHECK(tracker.full());
    sig.emit();
    CHECK(n == 2);
  }  // tracker dies -> both disconnect
  sig.emit();
  CHECK(n == 2);
  CHECK(sig.empty());
}

TEST_CASE("nexenne::signal::static_slot clear disconnects but stays usable") {
  auto sig{static_signal<void()>{}};
  auto n{0};
  auto tracker{static_slot<2>{}};
  static_cast<void>(sig.connect([&] noexcept { ++n; }, tracker));
  sig.emit();
  CHECK(n == 1);

  tracker.clear();
  CHECK(tracker.empty());
  sig.emit();
  CHECK(n == 1);  // detached

  static_cast<void>(sig.connect([&] noexcept { ++n; }, tracker));
  sig.emit();
  CHECK(n == 2);  // reusable
}

TEST_CASE("nexenne::signal::static_slot reports capacity and full state") {
  auto sig{static_signal<void(), 4>{}};
  auto tracker{static_slot<2>{}};
  CHECK(tracker.capacity() == 2);
  CHECK(tracker.empty());

  static_cast<void>(sig.connect([] noexcept {}, tracker));
  static_cast<void>(sig.connect([] noexcept {}, tracker));
  CHECK(tracker.full());
  CHECK(tracker.size() == 2);

  // Over-capacity connect: the connection stays live but untracked; the slot
  // is still added to the signal.
  static_cast<void>(sig.connect([] noexcept {}, tracker));
  CHECK(tracker.size() == 2);  // overflow ignored
  CHECK(sig.size() == 3);      // signal still got the slot
}

TEST_CASE("nexenne::signal::static_slot track returns false when full without disconnecting") {
  auto sig{static_signal<void()>{}};
  auto count{0};
  auto tracker{static_slot<1>{}};

  auto c1{sig.connect([&] noexcept { ++count; })};
  CHECK(tracker.track(c1));  // room for one

  auto const c2{sig.connect([&] noexcept { ++count; })};
  CHECK_FALSE(tracker.track(c2));  // full: reports false, must NOT disconnect c2
  CHECK(c2.has_target());

  sig.emit();
  CHECK(count == 2);  // both still fire
  static_cast<void>(c2);
}

namespace {
struct receiver {
  int hits{0};

  auto on_event(int const v) noexcept -> void {
    hits += v;
  }
};
}  // namespace

TEST_CASE("nexenne::signal::static_signal member-function connect shortcut") {
  auto sig{static_signal<void(int)>{}};
  auto r{receiver{}};
  auto const c{sig.connect<&receiver::on_event>(r)};
  static_cast<void>(c);

  sig.emit(3);
  sig.emit(4);
  CHECK(r.hits == 7);
}

TEST_CASE("nexenne::signal::static_signal member-function connect with static_slot ties lifetime") {
  auto sig{static_signal<void(int)>{}};
  auto r{receiver{}};
  {
    auto owner{static_slot<2>{}};
    static_cast<void>(sig.connect<&receiver::on_event>(r, owner));
    sig.emit(5);
    CHECK(r.hits == 5);
  }  // owner dropped
  sig.emit(10);
  CHECK(r.hits == 5);
}

TEST_CASE("nexenne::signal::static_sink exposes connect but hides emit") {
  auto sig{static_signal<void(int)>{}};
  auto sink{sig.as_sink()};
  auto got{0};
  auto const c{sink.connect([&](int v) noexcept { got = v; })};
  static_cast<void>(c);

  CHECK(sink.size() == 1);
  CHECK_FALSE(sink.empty());
  sig.emit(7);  // only the signal can fire
  CHECK(got == 7);
}

TEST_CASE("nexenne::signal::static_sink forwards connect_once, slot, and member overloads") {
  auto sig{static_signal<void(int)>{}};
  auto sink{sig.as_sink()};
  auto r{receiver{}};
  auto once_hits{0};

  auto const once{sink.connect_once([&](int v) noexcept { once_hits += v; })};
  static_cast<void>(once);
  {
    auto owner{static_slot<2>{}};
    static_cast<void>(sink.connect<&receiver::on_event>(r, owner));
    sig.emit(7);
    CHECK(r.hits == 7);
    CHECK(once_hits == 7);
  }  // owner dropped -> member slot detached
  sig.emit(11);
  CHECK(r.hits == 7);     // member slot gone
  CHECK(once_hits == 7);  // once slot already spent
}

TEST_CASE("nexenne::signal::static_sink reports full signal as over capacity via size") {
  auto sig{static_signal<void(), 1>{}};
  auto sink{sig.as_sink()};
  auto const a{sink.connect([] noexcept {})};
  CHECK(a.has_target());
  CHECK(sink.size() == 1);

  auto const b{sink.connect([] noexcept {})};  // full
  CHECK_FALSE(b.has_target());
  CHECK(sink.size() == 1);
}

TEST_CASE("nexenne::signal::static_signal stays consistent after full teardown and reuse") {
  auto sig{static_signal<void(), 4>{}};
  auto n{0};
  auto a{sig.connect([&] noexcept { ++n; })};
  auto b{sig.connect([&] noexcept { ++n; })};
  sig.emit();
  CHECK(n == 2);

  // Tear everything down explicitly, then reuse the same signal object.
  CHECK(a.disconnect());
  CHECK(b.disconnect());
  CHECK(sig.empty());

  auto const c{sig.connect([&] noexcept { n += 10; })};
  static_cast<void>(c);
  sig.emit();
  CHECK(n == 12);
}

TEST_CASE("nexenne::signal::static_connection equality compares signal and slot") {
  auto sig{static_signal<void()>{}};
  auto sig2{static_signal<void()>{}};
  auto const a{sig.connect([] noexcept {})};
  auto const b{sig.connect([] noexcept {})};
  auto const other{sig2.connect([] noexcept {})};

  auto const a_copy{a};
  CHECK(a == a_copy);                                 // same signal + slot id
  CHECK_FALSE(a == b);                                // same signal, different slot
  CHECK_FALSE(a == other);                            // different signal
  CHECK(static_connection{} == static_connection{});  // both target nothing
  CHECK_FALSE(a == static_connection{});
}

}  // namespace
