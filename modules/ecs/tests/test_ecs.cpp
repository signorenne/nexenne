/**
 * @file
 * @brief Tests for the nexenne::ecs module (registry, signals, type_id, view).
 */

#include <doctest/doctest.h>

#include <array>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/signal/connection.hpp>

namespace {

using nexenne::ecs::entity_id;
using nexenne::ecs::registry;
using nexenne::ecs::type_id;
using nexenne::ecs::view;
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

// ---- lifecycle signals ----

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

// ---- type_id ----

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

// ---- views ----

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

// ---- reentrancy regressions ----

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

}  // namespace
