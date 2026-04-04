/**
 * @file
 * @brief Tests for the nexenne::ecs module (registry, signals, type_id, view).
 */

#include <doctest/doctest.h>

#include <array>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/signal/connection.hpp>

namespace {

using nexenne::ecs::entity_id;
using nexenne::ecs::registry;
using nexenne::ecs::type_id;
using nexenne::ecs::view;
using nexenne::signal::connection;
using nexenne::signal::scoped_connection;

// Shared component types. position/velocity carry a z so both the 3-field
// (registry) and 2-field (signals/view) aggregate-init forms compile.
struct position {
  float x{};
  float y{};
  float z{};
};

struct velocity {
  float x{};
  float y{};
  float z{};
};

struct health {
  int hp{};
};

struct tag_player {};

struct alpha {};

struct beta {};

struct gamma {};

TEST_CASE("registry is empty by default") {
  auto const r{registry{}};
  CHECK(r.alive() == 0);
  CHECK_FALSE(r.valid(entity_id{}));
}

TEST_CASE("registry.create returns unique, valid entities") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  CHECK(r.alive() == 3);
  CHECK(r.valid(a));
  CHECK(r.valid(b));
  CHECK(r.valid(c));
  CHECK(a != b);
  CHECK(b != c);
}

TEST_CASE("registry.destroy invalidates the handle") {
  auto r{registry{}};
  auto const a{r.create()};
  CHECK(r.destroy(a));
  CHECK_FALSE(r.valid(a));
  CHECK_FALSE(r.destroy(a));  // double-destroy is a no-op
  CHECK(r.alive() == 0);
}

TEST_CASE("registry recycles destroyed indices with bumped generation") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const ai{a.index()};
  r.destroy(a);
  auto const b{r.create()};
  CHECK(b.index() == ai);  // index recycled
  CHECK(b.generation() != a.generation());
  CHECK(r.valid(b));
  CHECK_FALSE(r.valid(a));  // stale handle stays stale
}

TEST_CASE("registry.add / get / has / remove on a single component") {
  auto r{registry{}};
  auto const e{r.create()};

  CHECK(r.add<position>(e, {1.0f, 2.0f, 3.0f}));
  CHECK(r.has<position>(e));
  REQUIRE(r.get<position>(e).has_value());
  CHECK(r.get<position>(e).value().get().x == 1.0f);
  CHECK(r.get<position>(e).value().get().y == 2.0f);
  CHECK(r.get<position>(e).value().get().z == 3.0f);

  CHECK(r.remove<position>(e));
  CHECK_FALSE(r.has<position>(e));
  CHECK(!r.get<position>(e).has_value());
}

TEST_CASE("registry duplicate add replaces value") {
  auto r{registry{}};
  auto const e{r.create()};
  CHECK(r.add<position>(e, {1.0f, 2.0f, 3.0f}));
  CHECK_FALSE(r.add<position>(e, {10.0f, 20.0f, 30.0f}));
  CHECK(r.get<position>(e).value().get().x == 10.0f);
}

TEST_CASE("registry destroy removes all components") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<position>(e, {1.0f, 0.0f, 0.0f});
  r.add<velocity>(e, {0.0f, 1.0f, 0.0f});
  r.add<tag_player>(e, {});

  r.destroy(e);
  CHECK_FALSE(r.has<position>(e));
  CHECK_FALSE(r.has<velocity>(e));
  CHECK_FALSE(r.has<tag_player>(e));
}

TEST_CASE("registry storage<T>() iterates dense components") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  r.add<position>(a, {1.0f, 0.0f, 0.0f});
  r.add<position>(b, {2.0f, 0.0f, 0.0f});
  r.add<position>(c, {3.0f, 0.0f, 0.0f});

  auto sum{0.0f};
  for (auto const& p : r.storage<position>().values()) {
    sum += p.x;
  }
  CHECK(sum == 6.0f);
}

TEST_CASE("registry.get on stale handle returns nullptr") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<position>(e, {1.0f, 0.0f, 0.0f});
  r.destroy(e);
  CHECK(!r.get<position>(e).has_value());
  CHECK_FALSE(r.has<position>(e));
}

TEST_CASE("registry.add on default-constructed (sentinel) handle fails") {
  auto r{registry{}};
  auto const sentinel{entity_id{}};
  CHECK_FALSE(r.valid(sentinel));
  CHECK_FALSE(r.add<position>(sentinel, {}));
}

TEST_CASE("registry handles non-trivial component types (std::string)") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<std::string>(e, std::string{"hello"});
  REQUIRE(r.get<std::string>(e).has_value());
  CHECK(r.get<std::string>(e).value().get() == "hello");
  r.destroy(e);
  CHECK(!r.get<std::string>(e).has_value());
}

TEST_CASE("registry.clear bumps generations so old handles stay invalid") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f, 0.0f});
  r.clear();
  CHECK(r.alive() == 0);
  CHECK_FALSE(r.valid(a));

  // After clear, new entities can be created and start fresh.
  auto const b{r.create()};
  CHECK(r.valid(b));
  CHECK_FALSE(r.has<position>(b));
}

TEST_CASE("registry handles many entities with stable iteration") {
  auto r{registry{}};
  constexpr int N{1000};
  auto ents{std::vector<entity_id>{}};
  ents.reserve(N);
  for (auto i{0}; i < N; ++i) {
    auto const e{r.create()};
    ents.push_back(e);
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
  }
  CHECK(r.alive() == N);
  CHECK(r.storage<position>().size() == N);

  // Erase even indices.
  for (auto i{std::size_t{0}}; i < static_cast<std::size_t>(N); i += 2) {
    r.destroy(ents[i]);
  }
  CHECK(r.alive() == N / 2);
  CHECK(r.storage<position>().size() == N / 2);
}

TEST_CASE("registry iterates live entities") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};

  auto seen{std::vector<entity_id>{}};
  for (auto const e : r) {
    seen.push_back(e);
  }
  CHECK(seen.size() == 3);
  // Order is creation order (no destroys yet).
  CHECK(seen[0] == a);
  CHECK(seen[1] == b);
  CHECK(seen[2] == c);
}

TEST_CASE("registry iterator skips destroyed entities") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  r.destroy(b);

  auto count{0};
  for (auto const e : r) {
    CHECK(r.valid(e));
    CHECK(e != b);
    ++count;
  }
  CHECK(count == 2);
  CHECK(r.alive() == 2);
  (void)a;
  (void)c;
}

TEST_CASE("registry satisfies std::ranges::input_range") {
  static_assert(std::ranges::input_range<registry>);
}

TEST_CASE("registry iterator after recycle yields new generations") {
  auto r{registry{}};
  auto const a{r.create()};
  r.destroy(a);
  auto const b{r.create()};  // recycles a's slot

  auto seen{std::vector<entity_id>{}};
  for (auto const e : r) {
    seen.push_back(e);
  }
  REQUIRE(seen.size() == 1);
  CHECK(seen[0] == b);
  CHECK(seen[0] != a);  // generation bumped
}

TEST_CASE("empty registry iterator is end") {
  auto const r{registry{}};
  CHECK(r.begin() == r.end());
}

// lifecycle signals

TEST_CASE("registry on_construct fires after first add<T>") {
  auto r{registry{}};
  auto seen{std::vector<entity_id>{}};
  auto conn{r.on_construct<position>().connect([&](entity_id const e, position const&) noexcept {
    seen.push_back(e);
  })};

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 2.0f});

  REQUIRE(seen.size() == 1);
  CHECK(seen[0] == a);
  (void)conn;
}

TEST_CASE("registry on_construct does NOT fire on replacement") {
  auto r{registry{}};
  auto construct_count{0};
  auto conn{r.on_construct<position>().connect([&](entity_id, position const&) noexcept {
    ++construct_count;
  })};

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(a, {2.0f, 0.0f});  // replacement, not construct

  CHECK(construct_count == 1);
  (void)conn;
}

TEST_CASE("registry on_update fires when add replaces") {
  auto r{registry{}};
  auto update_count{0};
  auto last_value{0.0f};
  auto conn{r.on_update<position>().connect([&](entity_id, position const& p) noexcept {
    ++update_count;
    last_value = p.x;
  })};

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});  // construct, no update
  r.add<position>(a, {5.0f, 0.0f});  // replace -> on_update
  r.add<position>(a, {9.0f, 0.0f});  // replace -> on_update

  CHECK(update_count == 2);
  CHECK(last_value == 9.0f);
  (void)conn;
}

TEST_CASE("registry patch fires on_update with mutated value") {
  auto r{registry{}};
  auto seen_x{0.0f};
  auto conn{r.on_update<position>().connect([&](entity_id, position const& p) noexcept {
    seen_x = p.x;
  })};

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});

  auto const ok{r.patch<position>(a, [](position& p) noexcept { p.x = 42.0f; })};
  CHECK(ok);
  CHECK(seen_x == 42.0f);
  CHECK(r.get<position>(a).value().get().x == 42.0f);
  (void)conn;
}

TEST_CASE("patch on missing component returns false and does not fire") {
  auto r{registry{}};
  auto fired{false};
  auto conn{r.on_update<position>().connect([&](entity_id, position const&) noexcept {
    fired = true;
  })};
  auto const a{r.create()};
  auto const ok{r.patch<position>(a, [](position& p) noexcept { p.x = 1; })};
  CHECK_FALSE(ok);
  CHECK_FALSE(fired);
  (void)conn;
}

TEST_CASE("registry on_destroy fires before remove<T>") {
  auto r{registry{}};
  auto seen_value{0.0f};
  auto still_valid{false};
  auto conn{r.on_destroy<position>().connect([&](entity_id const e, position const& p) noexcept {
    seen_value = p.x;
    still_valid = e.generation() != 0;
  })};

  auto const a{r.create()};
  r.add<position>(a, {7.0f, 0.0f});
  CHECK(r.remove<position>(a));

  CHECK(seen_value == 7.0f);
  CHECK(still_valid);
  CHECK_FALSE(r.has<position>(a));
  (void)conn;
}

TEST_CASE("registry destroy fires on_destroy for every component") {
  auto r{registry{}};
  auto pos_count{0};
  auto vel_count{0};
  auto c1{r.on_destroy<position>().connect([&](entity_id, position const&) noexcept { ++pos_count; }
  )};
  auto c2{r.on_destroy<velocity>().connect([&](entity_id, velocity const&) noexcept { ++vel_count; }
  )};

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<velocity>(a, {2.0f, 0.0f});
  r.destroy(a);

  CHECK(pos_count == 1);
  CHECK(vel_count == 1);
  (void)c1;
  (void)c2;
}

TEST_CASE("destroy does NOT fire on_destroy for components the entity lacks") {
  auto r{registry{}};
  auto pos_count{0};
  auto vel_count{0};
  auto c1{r.on_destroy<position>().connect([&](entity_id, position const&) noexcept { ++pos_count; }
  )};
  auto c2{r.on_destroy<velocity>().connect([&](entity_id, velocity const&) noexcept { ++vel_count; }
  )};

  auto const a{r.create()};
  r.add<position>(a, {});
  // No velocity attached.
  r.destroy(a);

  CHECK(pos_count == 1);
  CHECK(vel_count == 0);
  (void)c1;
  (void)c2;
}

TEST_CASE("subscribing before any entity has the component works") {
  auto r{registry{}};
  auto fired{false};
  auto conn{r.on_construct<position>().connect([&](entity_id, position const&) noexcept {
    fired = true;
  })};

  auto const a{r.create()};
  r.add<position>(a, {});
  CHECK(fired);
  (void)conn;
}

TEST_CASE("scoped_connection auto-disconnects on scope exit") {
  auto r{registry{}};
  auto fire_count{0};
  {
    auto sc{scoped_connection{
      r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++fire_count; })
    }};

    auto const a{r.create()};
    r.add<position>(a, {});
  }  // sc destroyed; disconnected

  auto const b{r.create()};
  r.add<position>(b, {});
  CHECK(fire_count == 1);  // only the in-scope add fired
}

TEST_CASE("on_construct sink cannot fire the signal directly") {
  auto r{registry{}};
  auto sink{r.on_construct<position>()};
  // The sink type does not have emit() or operator() - this is a
  // compile-time guarantee, not a runtime check. We just verify the
  // sink exposes connect.
  auto conn{sink.connect([](entity_id, position const&) noexcept {})};
  (void)conn;
}

// type_id

TEST_CASE("type_id returns a stable value per type") {
  using nexenne::ecs::type_id;

  auto const a1{type_id<alpha>()};
  auto const a2{type_id<alpha>()};
  auto const b1{type_id<beta>()};
  auto const b2{type_id<beta>()};

  CHECK(a1 == a2);
  CHECK(b1 == b2);
  CHECK(a1 != b1);
}

TEST_CASE("type_id is dense (small consecutive IDs)") {
  using nexenne::ecs::type_id;

  // Reads of three distinct types should produce three distinct IDs.
  // The exact values depend on instantiation order across the whole
  // program, but each must be unique.
  auto const ids{std::array{type_id<alpha>(), type_id<beta>(), type_id<gamma>()}};
  CHECK(ids[0] != ids[1]);
  CHECK(ids[1] != ids[2]);
  CHECK(ids[0] != ids[2]);
}

TEST_CASE("type_id distinguishes cv-qualified variants") {
  using nexenne::ecs::type_id;
  // type_id treats T, const T, T&, etc. as distinct - they ARE
  // distinct template instantiations. Document this rather than
  // try to "normalise" the input.
  auto const a{type_id<int>()};
  auto const ar{type_id<int&>()};
  auto const ac{type_id<int const>()};
  // We don't assert relative ordering, just distinctness.
  CHECK(a != ar);
  CHECK(a != ac);
  CHECK(ar != ac);
}

// views

TEST_CASE("view single component iterates everything that has it") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(b, {2.0f, 0.0f});
  r.add<position>(c, {3.0f, 0.0f});

  auto sum{0.0f};
  view<position>{r}.each([&](position const& p) noexcept { sum += p.x; });
  CHECK(sum == 6.0f);
}

TEST_CASE("view<A, B> visits only entities with both") {
  auto r{registry{}};
  auto const a{r.create()};  // has pos+vel
  auto const b{r.create()};  // has pos only
  auto const c{r.create()};  // has vel only
  auto const d{r.create()};  // has both
  r.add<position>(a, {1.0f, 0.0f});
  r.add<velocity>(a, {10.0f, 0.0f});
  r.add<position>(b, {2.0f, 0.0f});
  r.add<velocity>(c, {20.0f, 0.0f});
  r.add<position>(d, {3.0f, 0.0f});
  r.add<velocity>(d, {30.0f, 0.0f});

  auto pos_sum{0.0f};
  auto vel_sum{0.0f};
  view<position, velocity>{r}.each([&](position const& p, velocity const& v) noexcept {
    pos_sum += p.x;
    vel_sum += v.x;
  });
  CHECK(pos_sum == 4.0f);   // a (1) + d (3)
  CHECK(vel_sum == 40.0f);  // a (10) + d (30)
}

TEST_CASE("view callback receives entity_id when requested") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<velocity>(a, {5.0f, 0.0f});

  auto visited{std::vector<entity_id>{}};
  view<position, velocity>{r}.each(
    [&](entity_id const e, position const&, velocity const&) noexcept { visited.push_back(e); }
  );
  REQUIRE(visited.size() == 1);
  CHECK(visited[0] == a);
}

TEST_CASE("view allows mutating components through references") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {0.0f, 0.0f});
  r.add<velocity>(a, {1.0f, 2.0f});
  r.add<position>(b, {0.0f, 0.0f});
  r.add<velocity>(b, {10.0f, 20.0f});

  view<position, velocity>{r}.each([](position& p, velocity const& v) noexcept {
    p.x += v.x;
    p.y += v.y;
  });
  CHECK(r.get<position>(a).value().get().x == 1.0f);
  CHECK(r.get<position>(a).value().get().y == 2.0f);
  CHECK(r.get<position>(b).value().get().x == 10.0f);
  CHECK(r.get<position>(b).value().get().y == 20.0f);
}

TEST_CASE("view<A, B, C> visits only entities with all three") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  r.add<position>(a, {});
  r.add<velocity>(a, {});
  r.add<health>(a, {100});

  r.add<position>(b, {});
  r.add<velocity>(b, {});

  r.add<position>(c, {});
  r.add<velocity>(c, {});
  r.add<health>(c, {50});

  auto count{0};
  view<position, velocity, health>{r}.each(
    [&](position const&, velocity const&, health const&) noexcept { ++count; }
  );
  CHECK(count == 2);  // a and c
}

TEST_CASE("view picks the smallest driver storage") {
  auto r{registry{}};
  // Many entities have position, only a few have health.
  auto rare_entities{std::vector<entity_id>{}};
  for (auto i{0}; i < 100; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {});
    if (i % 25 == 0) {  // 4 entities have health
      r.add<health>(e, {i});
      rare_entities.push_back(e);
    }
  }

  auto visited{0};
  view<position, health>{r}.each([&](position const&, health const&) noexcept { ++visited; });
  CHECK(visited == 4);
  // (The optimisation here is that the driver was the health storage
  // (size 4), not the position storage (size 100). Functional check
  // is the same regardless; absence of a regression in count
  // demonstrates correctness.)
}

TEST_CASE("view over empty registry yields nothing") {
  auto r{registry{}};
  auto count{0};
  view<position, velocity>{r}.each([&](position const&, velocity const&) noexcept { ++count; });
  CHECK(count == 0);
}

TEST_CASE("view is iterable with range-for and structured bindings") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<velocity>(a, {2.0f, 0.0f});
  r.add<position>(b, {3.0f, 0.0f});
  r.add<velocity>(b, {4.0f, 0.0f});

  auto pos_sum{0.0f};
  auto vel_sum{0.0f};
  for (auto [e, p, v] : view<position, velocity>{r}) {
    CHECK(r.valid(e));
    pos_sum += p.x;
    vel_sum += v.x;
  }
  CHECK(pos_sum == 4.0f);
  CHECK(vel_sum == 6.0f);
}

TEST_CASE("view satisfies std::ranges::input_range") {
  static_assert(std::ranges::input_range<view<position>>);
  static_assert(std::ranges::input_range<view<position, velocity>>);
}

TEST_CASE("view iterator default-constructible and comparable") {
  using iter_t = view<position>::iterator;
  static_assert(std::default_initializable<iter_t>);
  auto const a{iter_t{}};
  auto const b{iter_t{}};
  CHECK(a == b);
}

TEST_CASE("view + std::ranges::distance counts matches") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  r.add<position>(a, {});
  r.add<velocity>(a, {});
  r.add<position>(b, {});
  // c gets nothing.
  r.add<position>(c, {});
  r.add<velocity>(c, {});

  auto v{view<position, velocity>{r}};
  auto const n{std::ranges::distance(v)};
  CHECK(n == 2);
}

struct dead {};

struct frozen {};

TEST_CASE("view.exclude<T> filters out entities carrying T") {
  auto r{registry{}};
  auto const alive{r.create()};
  auto const corpse{r.create()};
  r.add<position>(alive, {1.0f, 0.0f});
  r.add<velocity>(alive, {});
  r.add<position>(corpse, {2.0f, 0.0f});
  r.add<velocity>(corpse, {});
  r.add<dead>(corpse, {});

  auto sum{0.0f};
  view<position, velocity>{r}.exclude<dead>().each(
    [&](position const& p, velocity const&) noexcept { sum += p.x; }
  );
  CHECK(sum == 1.0f);  // only alive
}

TEST_CASE("view.exclude with multiple excludes") {
  auto r{registry{}};
  auto const e1{r.create()};
  auto const e2{r.create()};
  auto const e3{r.create()};
  r.add<position>(e1, {});
  r.add<position>(e2, {});
  r.add<dead>(e2, {});
  r.add<position>(e3, {});
  r.add<frozen>(e3, {});

  auto count{0};
  view<position>{r}.exclude<dead, frozen>().each([&](position const&) noexcept { ++count; });
  CHECK(count == 1);  // only e1
}

TEST_CASE("registry.view<C>() member returns equivalent view") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(b, {2.0f, 0.0f});

  auto sum{0.0f};
  r.view<position>().each([&](position const& p) noexcept { sum += p.x; });
  CHECK(sum == 3.0f);
}

TEST_CASE("registry.each(callback) walks live entities") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.create();  // discarded handle

  auto count{0};
  r.each([&](entity_id const e) noexcept {
    CHECK(r.valid(e));
    ++count;
  });
  CHECK(count == 3);
  (void)a;
  (void)b;
}

TEST_CASE("registry.all_of / any_of") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<position>(e, {1.0f, 0.0f});

  CHECK(r.all_of<position>(e));
  CHECK_FALSE(r.all_of<position, velocity>(e));
  CHECK(r.any_of<position, velocity>(e));
  CHECK_FALSE(r.any_of<velocity, health>(e));

  r.add<velocity>(e, {});
  CHECK(r.all_of<position, velocity>(e));
}

TEST_CASE("query builder .with chain") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<velocity>(a, {});
  r.add<position>(b, {2.0f, 0.0f});
  // b lacks velocity

  auto sum{0.0f};
  r.query().with<position>().with<velocity>().each(
    [&](position const& p, velocity const&) noexcept { sum += p.x; }
  );
  CHECK(sum == 1.0f);  // only a
}

TEST_CASE("query builder .without chain") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(b, {2.0f, 0.0f});
  r.add<dead>(b, {});

  auto sum{0.0f};
  r.query().with<position>().without<dead>().each([&](position const& p) noexcept { sum += p.x; });
  CHECK(sum == 1.0f);  // only a
}

TEST_CASE("query builder .build returns a view") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {3.0f, 0.0f});
  r.add<velocity>(a, {});

  auto v{r.query().with<position>().with<velocity>().build()};
  auto count{0};
  for (auto [e, p, vel] : v) {
    ++count;
    CHECK(e == a);
    CHECK(p.x == 3.0f);
  }
  CHECK(count == 1);
}

TEST_CASE("query builder combining with + without + multiple") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  r.add<position>(a, {});
  r.add<velocity>(a, {});
  r.add<position>(b, {});
  r.add<velocity>(b, {});
  r.add<dead>(b, {});
  r.add<position>(c, {});
  r.add<velocity>(c, {});
  r.add<frozen>(c, {});

  auto count{0};
  r.query().with<position>().with<velocity>().without<dead>().without<frozen>().each(
    [&](position const&, velocity const&) noexcept { ++count; }
  );
  CHECK(count == 1);  // only a
}

TEST_CASE("view.exclude with range-for") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(b, {2.0f, 0.0f});
  r.add<dead>(b, {});

  auto sum{0.0f};
  for (auto [e, p] : view<position>{r}.exclude<dead>()) {
    sum += p.x;
  }
  CHECK(sum == 1.0f);
}

// reentrancy regressions

TEST_CASE("destroy is safe when an on_destroy listener registers a new component type") {
  auto r{registry{}};
  auto const victim{r.create()};
  auto const other{r.create()};
  r.add<position>(victim, {1.0f, 2.0f, 0.0f});

  // The listener attaches a brand-new component type during the destroy, which
  // creates a first-ever health storage and reallocates the type-erased storage
  // table mid-loop. Must not use-after-free.
  auto conn{r.on_destroy<position>().connect([&](entity_id, position const&) noexcept {
    r.add<health>(other, {42});
  })};
  (void)conn;

  CHECK(r.destroy(victim));
  CHECK_FALSE(r.valid(victim));
  CHECK(r.has<health>(other));
  REQUIRE(r.get<health>(other).has_value());
  CHECK(r.get<health>(other).value().get().hp == 42);
}

TEST_CASE("component references survive structural changes (pointer stability)") {
  auto r{registry{}};
  auto const pinned{r.create()};
  r.add<position>(pinned, {7.0f, 8.0f, 9.0f});
  auto* const addr{&r.get<position>(pinned).value().get()};

  // Grow the storage across many chunks and punch tombstone holes; a
  // pointer-stable pool must not relocate the pinned component.
  auto others{std::vector<entity_id>{}};
  others.reserve(2000);
  for (auto i{0}; i < 2000; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
    others.push_back(e);
  }
  for (auto i{std::size_t{0}}; i < others.size(); i += 2) {
    r.remove<position>(others[i]);
  }

  REQUIRE(r.get<position>(pinned).has_value());
  auto& after{r.get<position>(pinned).value().get()};
  CHECK(&after == addr);  // address never moved
  CHECK(after.x == 7.0f);
  CHECK(after.y == 8.0f);
  CHECK(after.z == 9.0f);
}

TEST_CASE("view.each may remove components mid-iteration without dangling") {
  auto r{registry{}};
  auto ents{std::vector<entity_id>{}};
  for (auto i{0}; i < 50; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
    r.add<health>(e, {i});
    ents.push_back(e);
  }

  // health and position are both size 50, so health (the first include) is
  // the driver. Removing the current entity's health tombstones the driver
  // slot just read; pointer stability keeps the references valid through the
  // rest of the body, and the captured slot count keeps the walk terminating.
  auto visited{0};
  auto sum{0.0f};
  view<health, position>{r}.each([&](entity_id const e, health const&, position const& p) noexcept {
    sum += p.x;
    ++visited;
    r.remove<health>(e);
  });
  CHECK(visited == 50);
  CHECK(sum == doctest::Approx(50.0 * 49.0 / 2.0));
  for (auto const e : ents) {
    CHECK_FALSE(r.has<health>(e));
    CHECK(r.has<position>(e));
  }
}

TEST_CASE("view.each may add components mid-iteration without dangling") {
  auto r{registry{}};
  for (auto i{0}; i < 30; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
  }

  // Adding positions to fresh entities grows the driver storage (possibly
  // allocating new chunks). Existing references stay valid, and the slots
  // appended mid-loop lie beyond the captured count, so they are not visited.
  auto visited{0};
  auto seen_sum{0.0f};
  view<position>{r}.each([&](position const& p) noexcept {
    seen_sum += p.x;
    ++visited;
    if (visited <= 5) {
      auto const fresh{r.create()};
      r.add<position>(fresh, {1000.0f, 0.0f, 0.0f});
    }
  });
  CHECK(visited == 30);  // only the originals, none of the 5 added mid-loop
  CHECK(seen_sum == doctest::Approx(30.0 * 29.0 / 2.0));
  CHECK(r.storage<position>().size() == 35);  // 30 + 5 now live
}

TEST_CASE("storage values() skips tombstoned slots after erase") {
  auto r{registry{}};
  auto ents{std::vector<entity_id>{}};
  for (auto i{0}; i < 10; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
    ents.push_back(e);
  }
  for (auto i{std::size_t{1}}; i < ents.size(); i += 2) {
    r.remove<position>(ents[i]);  // remove the odd-x components
  }

  auto sum{0.0f};
  auto count{0};
  for (auto const& p : r.storage<position>().values()) {
    sum += p.x;
    ++count;
  }
  CHECK(count == 5);
  CHECK(sum == doctest::Approx(0.0 + 2.0 + 4.0 + 6.0 + 8.0));
}

TEST_CASE("listener may structurally modify the same storage without dangling") {
  auto r{registry{}};
  auto const first{r.create()};

  // The on_construct listener attaches the SAME component type to many other
  // fresh entities, forcing the pool to grow new chunks while the reference it
  // was handed is still live. A flat-vector storage would reallocate and
  // dangle that reference; the pointer-stable pool must not.
  auto observed_x{-1.0f};
  auto* observed_addr{static_cast<position*>(nullptr)};
  auto conn{r.on_construct<position>().connect([&](entity_id const e, position& p) noexcept {
    if (e == first) {
      observed_addr = &p;
      for (auto i{0}; i < 500; ++i) {
        auto const other{r.create()};
        r.add<position>(other, {static_cast<float>(i), 0.0f, 0.0f});
      }
      observed_x = p.x;  // read AFTER the storage grew under us
    }
  })};
  static_cast<void>(conn);

  r.add<position>(first, {123.0f, 0.0f, 0.0f});
  CHECK(observed_x == 123.0f);  // reference stayed valid mid-growth
  REQUIRE(r.get<position>(first).has_value());
  auto& after{r.get<position>(first).value().get()};
  CHECK(&after == observed_addr);  // and never moved
  CHECK(after.x == 123.0f);
}

TEST_CASE("storage reuses tombstoned slots (no unbounded growth)") {
  auto r{registry{}};
  auto ents{std::vector<entity_id>{}};
  for (auto i{0}; i < 16; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
    ents.push_back(e);
  }
  auto const high_water{r.storage<position>().slot_count()};

  // Churn the same working set: every remove frees a slot the next add
  // reuses, so the slot count must not creep past the high-water mark.
  for (auto round{0}; round < 100; ++round) {
    for (auto const e : ents) {
      r.remove<position>(e);
    }
    for (auto const e : ents) {
      r.add<position>(e, {1.0f, 0.0f, 0.0f});
    }
  }
  CHECK(r.storage<position>().size() == 16);
  CHECK(r.storage<position>().slot_count() == high_water);
}

// added: type_id density

TEST_CASE("type_id assigns consecutive ids in first-touch order") {
  using nexenne::ecs::type_id;

  // First touch of three never-before-seen types: ids must be a run of
  // three consecutive values, in request order.
  struct fresh_a {};

  struct fresh_b {};

  struct fresh_c {};

  auto const a{type_id<fresh_a>()};
  auto const b{type_id<fresh_b>()};
  auto const c{type_id<fresh_c>()};
  CHECK(b == a + 1);
  CHECK(c == a + 2);
}

// added: lifecycle signals (uncovered angles)

TEST_CASE("multiple listeners on the same on_construct signal all fire") {
  auto r{registry{}};
  auto count_a{0};
  auto count_b{0};
  auto count_c{0};
  auto ca{r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++count_a; }
  )};
  auto cb{r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++count_b; }
  )};
  auto cc{r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++count_c; }
  )};

  auto const e{r.create()};
  r.add<position>(e, {});
  CHECK(count_a == 1);
  CHECK(count_b == 1);
  CHECK(count_c == 1);
  (void)ca;
  (void)cb;
  (void)cc;
}

TEST_CASE("manually disconnected connection stops firing") {
  auto r{registry{}};
  auto fire_count{0};
  auto conn{connection{r.on_construct<position>().connect([&](entity_id, position const&) noexcept {
    ++fire_count;
  })}};

  auto const a{r.create()};
  r.add<position>(a, {});
  CHECK(fire_count == 1);

  CHECK(conn.disconnect());
  auto const b{r.create()};
  r.add<position>(b, {});
  CHECK(fire_count == 1);          // no further fires after disconnect
  CHECK_FALSE(conn.disconnect());  // second disconnect is a no-op
}

TEST_CASE("one of several listeners can be dropped, the rest keep firing") {
  auto r{registry{}};
  auto kept{0};
  auto dropped{0};
  auto keep_conn{r.on_construct<position>().connect([&](entity_id, position const&) noexcept {
    ++kept;
  })};
  auto drop_conn{connection{
    r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++dropped; })
  }};

  r.add<position>(r.create(), {});
  CHECK(kept == 1);
  CHECK(dropped == 1);

  drop_conn.disconnect();
  r.add<position>(r.create(), {});
  CHECK(kept == 2);     // survivor keeps firing
  CHECK(dropped == 1);  // dropped one stayed silent
  (void)keep_conn;
}

TEST_CASE("on_update listener may read another component of the entity") {
  auto r{registry{}};
  auto observed_hp{-1};
  auto conn{r.on_update<position>().connect([&](entity_id const e, position const&) noexcept {
    if (auto const h{r.get<health>(e)}; h.has_value()) {
      observed_hp = h.value().get().hp;
    }
  })};

  auto const e{r.create()};
  r.add<health>(e, {77});
  r.add<position>(e, {1.0f, 0.0f});  // construct, no update
  r.add<position>(e, {2.0f, 0.0f});  // replace -> on_update reads health
  CHECK(observed_hp == 77);
  (void)conn;
}

// added: all_of / any_of edge cases

TEST_CASE("all_of with empty pack is true, any_of with empty pack is false") {
  auto r{registry{}};
  auto const e{r.create()};
  CHECK(r.all_of<>(e));
  CHECK_FALSE(r.any_of<>(e));
}

TEST_CASE("all_of / any_of with a single component") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<position>(e, {});
  CHECK(r.all_of<position>(e));
  CHECK(r.any_of<position>(e));
  CHECK_FALSE(r.all_of<velocity>(e));
  CHECK_FALSE(r.any_of<velocity>(e));
}

TEST_CASE("all_of / any_of with many components") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<position>(e, {});
  r.add<velocity>(e, {});
  r.add<health>(e, {});
  CHECK(r.all_of<position, velocity, health>(e));
  CHECK(r.any_of<position, velocity, health, tag_player>(e));
  CHECK_FALSE(r.all_of<position, velocity, health, tag_player>(e));
}

TEST_CASE("all_of / any_of on an invalid entity") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<position>(e, {});
  r.destroy(e);
  // A stale handle carries no components.
  CHECK_FALSE(r.all_of<position>(e));
  CHECK_FALSE(r.any_of<position>(e));
  // Empty pack still folds to its identity regardless of validity.
  CHECK(r.all_of<>(e));
  CHECK_FALSE(r.any_of<>(e));
}

// added: query builder edges

TEST_CASE("query builder single-include build equals the equivalent view") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(b, {2.0f, 0.0f});

  auto via_builder{0.0f};
  r.query().with<position>().each([&](position const& p) noexcept { via_builder += p.x; });

  auto via_view{0.0f};
  view<position>{r}.each([&](position const& p) noexcept { via_view += p.x; });

  CHECK(via_builder == via_view);
  CHECK(via_builder == 3.0f);
}

TEST_CASE("query builder length-3 include chain") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {});
  r.add<velocity>(a, {});
  r.add<health>(a, {1});
  r.add<position>(b, {});
  r.add<velocity>(b, {});
  // b lacks health

  auto count{0};
  r.query().with<position>().with<velocity>().with<health>().each(
    [&](position const&, velocity const&, health const&) noexcept { ++count; }
  );
  CHECK(count == 1);  // only a
}

TEST_CASE("query builder with no excludes (length-0 without chain) matches all") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {});
  r.add<position>(b, {});

  auto count{0};
  r.query().with<position>().each([&](position const&) noexcept { ++count; });
  CHECK(count == 2);
}

TEST_CASE("query builder yielding an empty result") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {});
  // No entity has both position and velocity.

  auto count{0};
  r.query().with<position>().with<velocity>().each([&](position const&, velocity const&) noexcept {
    ++count;
  });
  CHECK(count == 0);
}

// added: view edges

TEST_CASE("view over a component no entity has yields nothing") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {});
  // health storage exists only after this view touches it; nothing carries it.

  auto count{0};
  view<health>{r}.each([&](health const&) noexcept { ++count; });
  CHECK(count == 0);
}

TEST_CASE("view reflects a component lost between two iterations") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(b, {2.0f, 0.0f});

  auto first{0};
  view<position>{r}.each([&](position const&) noexcept { ++first; });
  CHECK(first == 2);

  r.remove<position>(a);  // a loses position between the two passes

  auto second_sum{0.0f};
  auto second{0};
  view<position>{r}.each([&](position const& p) noexcept {
    second_sum += p.x;
    ++second;
  });
  CHECK(second == 1);
  CHECK(second_sum == 2.0f);  // only b remains
}

TEST_CASE("view reflects a component gained between two iterations") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {});
  r.add<velocity>(a, {});
  r.add<position>(b, {});
  // b lacks velocity at first.

  auto first{0};
  view<position, velocity>{r}.each([&](position const&, velocity const&) noexcept { ++first; });
  CHECK(first == 1);  // only a

  r.add<velocity>(b, {});  // b gains velocity

  auto second{0};
  view<position, velocity>{r}.each([&](position const&, velocity const&) noexcept { ++second; });
  CHECK(second == 2);  // a and b
}

TEST_CASE("view smallest-driver selection with 3+ includes is correct") {
  auto r{registry{}};
  // position: many, velocity: medium, health: few. The driver must be the
  // health storage (smallest), and the count must still be the true triple
  // intersection.
  auto triple{std::vector<entity_id>{}};
  for (auto i{0}; i < 60; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
    if (i % 2 == 0) {
      r.add<velocity>(e, {});
    }
    if (i % 20 == 0) {  // i = 0, 20, 40 -> all even, so they also have velocity
      r.add<health>(e, {i});
      triple.push_back(e);
    }
  }

  auto count{0};
  view<position, velocity, health>{r}.each(
    [&](position const&, velocity const&, health const&) noexcept { ++count; }
  );
  CHECK(count == static_cast<int>(triple.size()));
  CHECK(count == 3);
}

// added: registry move + clear/reuse

TEST_CASE("registry move construction transfers ownership") {
  auto src{registry{}};
  auto const a{src.create()};
  auto const b{src.create()};
  src.add<position>(a, {1.0f, 2.0f, 3.0f});
  src.add<velocity>(a, {4.0f, 0.0f, 0.0f});
  src.add<position>(b, {5.0f, 0.0f, 0.0f});

  auto dst{registry{std::move(src)}};

  // Moved-from registry is empty and reusable.
  CHECK(src.alive() == 0);  // NOLINT(bugprone-use-after-move)
  auto const fresh{src.create()};
  CHECK(src.valid(fresh));

  // Destination owns the entities and their components intact.
  CHECK(dst.alive() == 2);
  CHECK(dst.valid(a));
  CHECK(dst.valid(b));
  REQUIRE(dst.get<position>(a).has_value());
  CHECK(dst.get<position>(a).value().get().x == 1.0f);
  CHECK(dst.get<position>(a).value().get().z == 3.0f);
  REQUIRE(dst.get<velocity>(a).has_value());
  CHECK(dst.get<velocity>(a).value().get().x == 4.0f);
  CHECK(dst.get<position>(b).value().get().x == 5.0f);
}

TEST_CASE("registry move assignment transfers ownership and frees the target") {
  auto src{registry{}};
  auto const a{src.create()};
  src.add<position>(a, {7.0f, 0.0f, 0.0f});

  auto dst{registry{}};
  auto const old{dst.create()};
  dst.add<health>(old, {99});  // these storages must be freed by the assignment

  dst = std::move(src);

  CHECK(dst.alive() == 1);
  CHECK(dst.valid(a));
  // `old` and `a` are both {index 0, generation 1} (each registry's first
  // entity), so they collide as handles (a handle carries no registry id);
  // verify instead that dst's OLD storages were freed: the health it held is
  // gone (the moved-in registry had no health storage).
  CHECK_FALSE(dst.has<health>(old));
  REQUIRE(dst.get<position>(a).has_value());
  CHECK(dst.get<position>(a).value().get().x == 7.0f);
  CHECK(src.alive() == 0);  // NOLINT(bugprone-use-after-move)
}

TEST_CASE("registry self-move-assignment is a safe no-op") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {3.0f, 0.0f, 0.0f});

  auto& alias{r};
  r = std::move(alias);  // NOLINT(clang-diagnostic-self-move)

  CHECK(r.alive() == 1);
  CHECK(r.valid(a));
  REQUIRE(r.get<position>(a).has_value());
  CHECK(r.get<position>(a).value().get().x == 3.0f);
}

TEST_CASE("registry clear then reuse keeps storages working") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f, 0.0f});
  r.add<velocity>(a, {2.0f, 0.0f, 0.0f});
  r.clear();
  CHECK(r.alive() == 0);
  CHECK(r.storage<position>().size() == 0);
  CHECK(r.storage<velocity>().size() == 0);

  // The same storages accept fresh components after the clear.
  auto const b{r.create()};
  r.add<position>(b, {10.0f, 0.0f, 0.0f});
  CHECK(r.alive() == 1);
  CHECK(r.storage<position>().size() == 1);
  REQUIRE(r.get<position>(b).has_value());
  CHECK(r.get<position>(b).value().get().x == 10.0f);
  CHECK_FALSE(r.has<position>(a));  // old handle stays detached
}

// added: non-trivial / move-only components

TEST_CASE("std::string components do not leak on remove / clear / destroy") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  // Long strings force heap allocation (defeating SSO), so LSan/ASan catches
  // any leaked buffer on the three teardown paths below.
  r.add<std::string>(a, std::string(64, 'a'));
  r.add<std::string>(b, std::string(64, 'b'));
  r.add<std::string>(c, std::string(64, 'c'));

  CHECK(r.remove<std::string>(a));  // remove path
  r.destroy(b);                     // destroy path
  // c's string is freed by clear, exercising the third teardown path.
  r.clear();
  CHECK(r.storage<std::string>().size() == 0);
}

TEST_CASE("std::string component replace assigns in place without leaking") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<std::string>(e, std::string(48, 'x'));
  CHECK_FALSE(r.add<std::string>(e, std::string(48, 'y')));  // replace
  REQUIRE(r.get<std::string>(e).has_value());
  CHECK(r.get<std::string>(e).value().get() == std::string(48, 'y'));
}

TEST_CASE("move-only component type is supported") {
  auto r{registry{}};
  auto const a{r.create()};
  // add<T>(entity, T) takes the value by move, so a move-only payload works.
  r.add<std::unique_ptr<int>>(a, std::make_unique<int>(42));
  REQUIRE(r.get<std::unique_ptr<int>>(a).has_value());
  REQUIRE(r.get<std::unique_ptr<int>>(a).value().get() != nullptr);
  CHECK(*r.get<std::unique_ptr<int>>(a).value().get() == 42);

  // patch mutates in place through a reference (no copy required).
  CHECK(r.patch<std::unique_ptr<int>>(a, [](std::unique_ptr<int>& p) noexcept { *p = 7; }));
  CHECK(*r.get<std::unique_ptr<int>>(a).value().get() == 7);

  r.destroy(a);  // unique_ptr freed without a leak
  CHECK(!r.get<std::unique_ptr<int>>(a).has_value());
}

// added: recycle / stale-handle safety

TEST_CASE("stale handle after recycle reads invalid for get / has / remove") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f, 0.0f});
  r.destroy(a);
  auto const b{r.create()};  // recycles a's slot with a bumped generation
  REQUIRE(b.index() == a.index());
  r.add<position>(b, {9.0f, 0.0f, 0.0f});

  // The stale handle must not read or touch the recycled slot's component.
  CHECK_FALSE(r.has<position>(a));
  CHECK(!r.get<position>(a).has_value());
  CHECK_FALSE(r.remove<position>(a));  // does not strip b's component
  CHECK(r.has<position>(b));
  CHECK(r.get<position>(b).value().get().x == 9.0f);
}

TEST_CASE("recycle never mints a generation-0 (invalid) handle") {
  auto r{registry{}};
  // Churn one slot many times; every recycled handle must stay valid (the
  // registry steps the generation over 0 on wraparound, so no live handle
  // ever collides with the default-constructed sentinel).
  auto last{entity_id{}};
  for (auto i{0}; i < 1000; ++i) {
    auto const e{r.create()};
    CHECK(e.generation() != 0);
    CHECK(r.valid(e));
    r.destroy(e);
    last = e;
  }
  CHECK_FALSE(r.valid(last));
  CHECK_FALSE(r.valid(entity_id{}));
}

TEST_CASE(
  "registry.destroy detaches a component an on_destroy listener attaches to the dying entity"
) {
  auto r{registry{}};
  // The dying entity is still valid while on_destroy fires, so a listener can
  // attach a brand-new component type to it. Its storage does not exist until
  // this fires (so it is appended past the storages the destroy loop captured).
  // destroy must still leave the freed index component-free.
  auto conn{r.on_destroy<health>().connect([&](entity_id const e, health const&) noexcept {
    static_cast<void>(r.add<position>(e, position{.x = 9.0F, .y = 9.0F, .z = 9.0F}));
  })};
  auto const a{r.create()};
  static_cast<void>(r.add<health>(a, health{.hp = 100}));
  auto const a_index{a.index()};

  CHECK(r.destroy(a));

  auto const b{r.create()};
  REQUIRE(b.index() == a_index);    // recycled the freed index
  CHECK_FALSE(r.has<position>(b));  // the mid-destroy attachment was swept off
}

}  // namespace
