/**
 * @file
 * @brief Tests for the nexenne::ecs module (registry, signals, type_id, view).
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/ecs/ecs.hpp>
#include <nexenne/signal/connection.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

using nexenne::ecs::entity_id;
using nexenne::ecs::registry;
using nexenne::ecs::type_id;
using nexenne::ecs::view;
using nexenne::signal::connection;
using nexenne::signal::scoped_connection;

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
  CHECK_FALSE(r.destroy(a));
  CHECK(r.alive() == 0);
}

TEST_CASE("registry recycles destroyed indices with bumped generation") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const ai{a.index()};
  r.destroy(a);
  auto const b{r.create()};
  CHECK(b.index() == ai);
  CHECK(b.generation() != a.generation());
  CHECK(r.valid(b));
  CHECK_FALSE(r.valid(a));
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

  for (auto i{std::size_t{0}}; i < static_cast<std::size_t>(N); i += 2) {
    r.destroy(ents[i]);
  }
  CHECK(r.alive() == N / 2);
  CHECK(r.storage<position>().size() == N / 2);
}

TEST_CASE("registry iterates live entities in creation order") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};

  auto seen{std::vector<entity_id>{}};
  for (auto const e : r) {
    seen.push_back(e);
  }
  CHECK(seen.size() == 3);
  CHECK(seen[0] == a);
  CHECK(seen[1] == b);
  CHECK(seen[2] == c);
}

TEST_CASE("registry iterator skips destroyed entities") {
  auto r{registry{}};
  [[maybe_unused]] auto const a{r.create()};
  auto const b{r.create()};
  [[maybe_unused]] auto const c{r.create()};
  r.destroy(b);

  auto count{0};
  for (auto const e : r) {
    CHECK(r.valid(e));
    CHECK(e != b);
    ++count;
  }
  CHECK(count == 2);
  CHECK(r.alive() == 2);
}

TEST_CASE("registry satisfies std::ranges::input_range") {
  static_assert(std::ranges::input_range<registry>);
}

TEST_CASE("registry iterator after recycle yields new generations") {
  auto r{registry{}};
  auto const a{r.create()};
  r.destroy(a);
  auto const b{r.create()};

  auto seen{std::vector<entity_id>{}};
  for (auto const e : r) {
    seen.push_back(e);
  }
  REQUIRE(seen.size() == 1);
  CHECK(seen[0] == b);
  CHECK(seen[0] != a);
}

TEST_CASE("empty registry iterator is end") {
  auto const r{registry{}};
  CHECK(r.begin() == r.end());
}

TEST_CASE("registry on_construct fires after first add<T>") {
  auto r{registry{}};
  auto seen{std::vector<entity_id>{}};
  [[maybe_unused]] auto conn{r.on_construct<position>().connect(
    [&](entity_id const e, position const&) noexcept { seen.push_back(e); }
  )};

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 2.0f});

  REQUIRE(seen.size() == 1);
  CHECK(seen[0] == a);
}

TEST_CASE("registry on_construct does NOT fire on replacement") {
  auto r{registry{}};
  auto construct_count{0};
  [[maybe_unused]] auto conn{r.on_construct<position>().connect(
    [&](entity_id, position const&) noexcept { ++construct_count; }
  )};

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(a, {2.0f, 0.0f});

  CHECK(construct_count == 1);
}

TEST_CASE("registry on_update fires when add replaces") {
  auto r{registry{}};
  auto update_count{0};
  auto last_value{0.0f};
  [[maybe_unused]] auto conn{
    r.on_update<position>().connect([&](entity_id, position const& p) noexcept {
      ++update_count;
      last_value = p.x;
    })
  };

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<position>(a, {5.0f, 0.0f});
  r.add<position>(a, {9.0f, 0.0f});

  CHECK(update_count == 2);
  CHECK(last_value == 9.0f);
}

TEST_CASE("registry patch fires on_update with mutated value") {
  auto r{registry{}};
  auto seen_x{0.0f};
  [[maybe_unused]] auto conn{
    r.on_update<position>().connect([&](entity_id, position const& p) noexcept { seen_x = p.x; })
  };

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});

  auto const ok{r.patch<position>(a, [](position& p) noexcept { p.x = 42.0f; })};
  CHECK(ok);
  CHECK(seen_x == 42.0f);
  CHECK(r.get<position>(a).value().get().x == 42.0f);
}

TEST_CASE("patch on missing component returns false and does not fire") {
  auto r{registry{}};
  auto fired{false};
  [[maybe_unused]] auto conn{
    r.on_update<position>().connect([&](entity_id, position const&) noexcept { fired = true; })
  };
  auto const a{r.create()};
  auto const ok{r.patch<position>(a, [](position& p) noexcept { p.x = 1; })};
  CHECK_FALSE(ok);
  CHECK_FALSE(fired);
}

TEST_CASE("registry on_destroy fires before remove<T>") {
  auto r{registry{}};
  auto seen_value{0.0f};
  auto still_valid{false};
  [[maybe_unused]] auto conn{
    r.on_destroy<position>().connect([&](entity_id const e, position const& p) noexcept {
      seen_value = p.x;
      still_valid = e.generation() != 0;
    })
  };

  auto const a{r.create()};
  r.add<position>(a, {7.0f, 0.0f});
  CHECK(r.remove<position>(a));

  CHECK(seen_value == 7.0f);
  CHECK(still_valid);
  CHECK_FALSE(r.has<position>(a));
}

TEST_CASE("registry destroy fires on_destroy for every component") {
  auto r{registry{}};
  auto pos_count{0};
  auto vel_count{0};
  [[maybe_unused]] auto c1{
    r.on_destroy<position>().connect([&](entity_id, position const&) noexcept { ++pos_count; })
  };
  [[maybe_unused]] auto c2{
    r.on_destroy<velocity>().connect([&](entity_id, velocity const&) noexcept { ++vel_count; })
  };

  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f});
  r.add<velocity>(a, {2.0f, 0.0f});
  r.destroy(a);

  CHECK(pos_count == 1);
  CHECK(vel_count == 1);
}

TEST_CASE("destroy does NOT fire on_destroy for components the entity lacks") {
  auto r{registry{}};
  auto pos_count{0};
  auto vel_count{0};
  [[maybe_unused]] auto c1{
    r.on_destroy<position>().connect([&](entity_id, position const&) noexcept { ++pos_count; })
  };
  [[maybe_unused]] auto c2{
    r.on_destroy<velocity>().connect([&](entity_id, velocity const&) noexcept { ++vel_count; })
  };

  auto const a{r.create()};
  r.add<position>(a, {});
  r.destroy(a);

  CHECK(pos_count == 1);
  CHECK(vel_count == 0);
}

TEST_CASE("subscribing before any entity has the component works") {
  auto r{registry{}};
  auto fired{false};
  [[maybe_unused]] auto conn{
    r.on_construct<position>().connect([&](entity_id, position const&) noexcept { fired = true; })
  };

  auto const a{r.create()};
  r.add<position>(a, {});
  CHECK(fired);
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
  }

  auto const b{r.create()};
  r.add<position>(b, {});
  CHECK(fire_count == 1);
}

TEST_CASE("on_construct sink cannot fire the signal directly") {
  auto r{registry{}};
  auto sink{r.on_construct<position>()};
  [[maybe_unused]] auto conn{sink.connect([](entity_id, position const&) noexcept {})};
}

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

TEST_CASE("type_id gives distinct types distinct ids") {
  using nexenne::ecs::type_id;

  auto const ids{std::array{type_id<alpha>(), type_id<beta>(), type_id<gamma>()}};
  CHECK(ids[0] != ids[1]);
  CHECK(ids[1] != ids[2]);
  CHECK(ids[0] != ids[2]);
}

TEST_CASE("type_id distinguishes cv-qualified variants") {
  using nexenne::ecs::type_id;
  auto const a{type_id<int>()};
  auto const ar{type_id<int&>()};
  auto const ac{type_id<int const>()};
  CHECK(a != ar);
  CHECK(a != ac);
  CHECK(ar != ac);
}

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
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  auto const d{r.create()};
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
  CHECK(pos_sum == 4.0f);
  CHECK(vel_sum == 40.0f);
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
  CHECK(count == 2);
}

TEST_CASE("view driven by its smallest storage still visits every match") {
  auto r{registry{}};
  auto rare_entities{std::vector<entity_id>{}};
  for (auto i{0}; i < 100; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {});
    if (i % 25 == 0) {
      r.add<health>(e, {i});
      rare_entities.push_back(e);
    }
  }

  auto visited{0};
  view<position, health>{r}.each([&](position const&, health const&) noexcept { ++visited; });
  CHECK(visited == 4);
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
  CHECK(sum == 1.0f);
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
  CHECK(count == 1);
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
  [[maybe_unused]] auto const a{r.create()};
  [[maybe_unused]] auto const b{r.create()};
  r.create();

  auto count{0};
  r.each([&](entity_id const e) noexcept {
    CHECK(r.valid(e));
    ++count;
  });
  CHECK(count == 3);
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

  auto sum{0.0f};
  r.query().with<position>().with<velocity>().each(
    [&](position const& p, velocity const&) noexcept { sum += p.x; }
  );
  CHECK(sum == 1.0f);
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
  CHECK(sum == 1.0f);
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
  CHECK(count == 1);
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

TEST_CASE("destroy is safe when an on_destroy listener registers a new component type") {
  auto r{registry{}};
  auto const victim{r.create()};
  auto const other{r.create()};
  r.add<position>(victim, {1.0f, 2.0f, 0.0f});

  [[maybe_unused]] auto conn{r.on_destroy<position>().connect(
    [&](entity_id, position const&) noexcept { r.add<health>(other, {42}); }
  )};

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
  CHECK(&after == addr);
  CHECK(after.x == 7.0f);
  CHECK(after.y == 8.0f);
  CHECK(after.z == 9.0f);
}

TEST_CASE("view.each may remove the driver component mid-iteration without dangling") {
  auto r{registry{}};
  auto ents{std::vector<entity_id>{}};
  for (auto i{0}; i < 50; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
    r.add<health>(e, {i});
    ents.push_back(e);
  }

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

TEST_CASE("view.each may add components mid-iteration and does not visit them") {
  auto r{registry{}};
  for (auto i{0}; i < 30; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
  }

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
  CHECK(visited == 30);
  CHECK(seen_sum == doctest::Approx(30.0 * 29.0 / 2.0));
  CHECK(r.storage<position>().size() == 35);
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
    r.remove<position>(ents[i]);
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

  auto observed_x{-1.0f};
  auto* observed_addr{static_cast<position*>(nullptr)};
  [[maybe_unused]] auto conn{
    r.on_construct<position>().connect([&](entity_id const e, position& p) noexcept {
      if (e == first) {
        observed_addr = &p;
        for (auto i{0}; i < 500; ++i) {
          auto const other{r.create()};
          r.add<position>(other, {static_cast<float>(i), 0.0f, 0.0f});
        }
        observed_x = p.x;
      }
    })
  };

  r.add<position>(first, {123.0f, 0.0f, 0.0f});
  CHECK(observed_x == 123.0f);
  REQUIRE(r.get<position>(first).has_value());
  auto& after{r.get<position>(first).value().get()};
  CHECK(&after == observed_addr);
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

TEST_CASE("type_id assigns consecutive ids in first-touch order") {
  using nexenne::ecs::type_id;

  struct fresh_a {};

  struct fresh_b {};

  struct fresh_c {};

  auto const a{type_id<fresh_a>()};
  auto const b{type_id<fresh_b>()};
  auto const c{type_id<fresh_c>()};
  CHECK(b == a + 1);
  CHECK(c == a + 2);
}

TEST_CASE("multiple listeners on the same on_construct signal all fire") {
  auto r{registry{}};
  auto count_a{0};
  auto count_b{0};
  auto count_c{0};
  [[maybe_unused]] auto ca{
    r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++count_a; })
  };
  [[maybe_unused]] auto cb{
    r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++count_b; })
  };
  [[maybe_unused]] auto cc{
    r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++count_c; })
  };

  auto const e{r.create()};
  r.add<position>(e, {});
  CHECK(count_a == 1);
  CHECK(count_b == 1);
  CHECK(count_c == 1);
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
  CHECK(fire_count == 1);
  CHECK_FALSE(conn.disconnect());
}

TEST_CASE("one of several listeners can be dropped, the rest keep firing") {
  auto r{registry{}};
  auto kept{0};
  auto dropped{0};
  [[maybe_unused]] auto keep_conn{
    r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++kept; })
  };
  auto drop_conn{connection{
    r.on_construct<position>().connect([&](entity_id, position const&) noexcept { ++dropped; })
  }};

  r.add<position>(r.create(), {});
  CHECK(kept == 1);
  CHECK(dropped == 1);

  drop_conn.disconnect();
  r.add<position>(r.create(), {});
  CHECK(kept == 2);
  CHECK(dropped == 1);
}

TEST_CASE("on_update listener may read another component of the entity") {
  auto r{registry{}};
  auto observed_hp{-1};
  [[maybe_unused]] auto conn{
    r.on_update<position>().connect([&](entity_id const e, position const&) noexcept {
      if (auto const h{r.get<health>(e)}; h.has_value()) {
        observed_hp = h.value().get().hp;
      }
    })
  };

  auto const e{r.create()};
  r.add<health>(e, {77});
  r.add<position>(e, {1.0f, 0.0f});
  r.add<position>(e, {2.0f, 0.0f});
  CHECK(observed_hp == 77);
}

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
  CHECK_FALSE(r.all_of<position>(e));
  CHECK_FALSE(r.any_of<position>(e));
  CHECK(r.all_of<>(e));
  CHECK_FALSE(r.any_of<>(e));
}

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

  auto count{0};
  r.query().with<position>().with<velocity>().with<health>().each(
    [&](position const&, velocity const&, health const&) noexcept { ++count; }
  );
  CHECK(count == 1);
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

  auto count{0};
  r.query().with<position>().with<velocity>().each([&](position const&, velocity const&) noexcept {
    ++count;
  });
  CHECK(count == 0);
}

TEST_CASE("view over a never-registered component yields nothing") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {});

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

  r.remove<position>(a);

  auto second_sum{0.0f};
  auto second{0};
  view<position>{r}.each([&](position const& p) noexcept {
    second_sum += p.x;
    ++second;
  });
  CHECK(second == 1);
  CHECK(second_sum == 2.0f);
}

TEST_CASE("view reflects a component gained between two iterations") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  r.add<position>(a, {});
  r.add<velocity>(a, {});
  r.add<position>(b, {});

  auto first{0};
  view<position, velocity>{r}.each([&](position const&, velocity const&) noexcept { ++first; });
  CHECK(first == 1);

  r.add<velocity>(b, {});

  auto second{0};
  view<position, velocity>{r}.each([&](position const&, velocity const&) noexcept { ++second; });
  CHECK(second == 2);
}

TEST_CASE("view smallest-driver selection with 3+ includes is correct") {
  auto r{registry{}};
  auto triple{std::vector<entity_id>{}};
  for (auto i{0}; i < 60; ++i) {
    auto const e{r.create()};
    r.add<position>(e, {static_cast<float>(i), 0.0f, 0.0f});
    if (i % 2 == 0) {
      r.add<velocity>(e, {});
    }
    if (i % 20 == 0) {
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

TEST_CASE("registry move construction transfers ownership") {
  auto src{registry{}};
  auto const a{src.create()};
  auto const b{src.create()};
  src.add<position>(a, {1.0f, 2.0f, 3.0f});
  src.add<velocity>(a, {4.0f, 0.0f, 0.0f});
  src.add<position>(b, {5.0f, 0.0f, 0.0f});

  auto dst{registry{std::move(src)}};

  CHECK(src.alive() == 0);  // NOLINT(bugprone-use-after-move)
  auto const fresh{src.create()};
  CHECK(src.valid(fresh));

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
  dst.add<health>(old, {99});

  dst = std::move(src);

  CHECK(dst.alive() == 1);
  CHECK(dst.valid(a));
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

  auto const b{r.create()};
  r.add<position>(b, {10.0f, 0.0f, 0.0f});
  CHECK(r.alive() == 1);
  CHECK(r.storage<position>().size() == 1);
  REQUIRE(r.get<position>(b).has_value());
  CHECK(r.get<position>(b).value().get().x == 10.0f);
  CHECK_FALSE(r.has<position>(a));
}

TEST_CASE("std::string components do not leak on remove / clear / destroy") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  auto const c{r.create()};
  r.add<std::string>(a, std::string(64, 'a'));
  r.add<std::string>(b, std::string(64, 'b'));
  r.add<std::string>(c, std::string(64, 'c'));

  CHECK(r.remove<std::string>(a));
  r.destroy(b);
  r.clear();
  CHECK(r.storage<std::string>().size() == 0);
}

TEST_CASE("std::string component replace assigns in place without leaking") {
  auto r{registry{}};
  auto const e{r.create()};
  r.add<std::string>(e, std::string(48, 'x'));
  CHECK_FALSE(r.add<std::string>(e, std::string(48, 'y')));
  REQUIRE(r.get<std::string>(e).has_value());
  CHECK(r.get<std::string>(e).value().get() == std::string(48, 'y'));
}

TEST_CASE("move-only component type is supported") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<std::unique_ptr<int>>(a, std::make_unique<int>(42));
  REQUIRE(r.get<std::unique_ptr<int>>(a).has_value());
  REQUIRE(r.get<std::unique_ptr<int>>(a).value().get() != nullptr);
  CHECK(*r.get<std::unique_ptr<int>>(a).value().get() == 42);

  CHECK(r.patch<std::unique_ptr<int>>(a, [](std::unique_ptr<int>& p) noexcept { *p = 7; }));
  CHECK(*r.get<std::unique_ptr<int>>(a).value().get() == 7);

  r.destroy(a);
  CHECK(!r.get<std::unique_ptr<int>>(a).has_value());
}

TEST_CASE("stale handle after recycle reads invalid for get / has / remove") {
  auto r{registry{}};
  auto const a{r.create()};
  r.add<position>(a, {1.0f, 0.0f, 0.0f});
  r.destroy(a);
  auto const b{r.create()};
  REQUIRE(b.index() == a.index());
  r.add<position>(b, {9.0f, 0.0f, 0.0f});

  CHECK_FALSE(r.has<position>(a));
  CHECK(!r.get<position>(a).has_value());
  CHECK_FALSE(r.remove<position>(a));
  CHECK(r.has<position>(b));
  CHECK(r.get<position>(b).value().get().x == 9.0f);
}

TEST_CASE("recycle never mints a generation-0 (invalid) handle") {
  auto r{registry{}};
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

TEST_CASE("registry.destroy rejects a listener re-adding to the dying (now-invalid) entity") {
  auto r{registry{}};
  auto add_result{true};
  [[maybe_unused]] auto conn{
    r.on_destroy<health>().connect([&](entity_id const e, health const&) noexcept {
      add_result = r.add<position>(e, position{.x = 9.0F, .y = 9.0F, .z = 9.0F});
    })
  };
  auto const a{r.create()};
  nexenne::utility::ignore(r.add<health>(a, health{.hp = 100}));
  auto const a_index{a.index()};

  CHECK(r.destroy(a));
  CHECK_FALSE(add_result);

  auto const b{r.create()};
  REQUIRE(b.index() == a_index);
  CHECK_FALSE(r.has<position>(b));
}

TEST_CASE("values() range-for tolerates append then tombstone mid-iteration") {
  auto r{registry{}};
  auto const e0{r.create()};
  auto const e1{r.create()};
  nexenne::utility::ignore(r.add<health>(e0, health{.hp = 1}));
  nexenne::utility::ignore(r.add<health>(e1, health{.hp = 2}));

  auto visited{0};
  auto sum{0};
  auto mutated{false};
  for (auto& h : r.storage<health>().values()) {
    ++visited;
    sum += h.hp;
    if (!mutated) {
      mutated = true;
      auto const tmp{r.create()};
      nexenne::utility::ignore(r.add<health>(tmp, health{.hp = 99}));
      nexenne::utility::ignore(r.remove<health>(tmp));
    }
  }
  CHECK(visited == 2);
  CHECK(sum == 3);
}

TEST_CASE("valid() rejects a forged handle carrying a freed slot's current generation") {
  auto r{registry{}};
  auto const a{r.create()};
  auto const idx{a.index()};
  CHECK(r.destroy(a));
  CHECK(r.alive() == 0);

  auto const forged{entity_id{idx, r.generation_at(idx)}};
  CHECK_FALSE(r.valid(forged));
  CHECK_FALSE(r.add<health>(forged, health{.hp = 5}));
  CHECK_FALSE(r.destroy(forged));

  auto const b{r.create()};
  auto const c{r.create()};
  CHECK(b.index() != c.index());
}

TEST_CASE("an on_destroy listener destroying the same entity does not recurse") {
  auto r{registry{}};
  auto reentry_result{true};
  auto fire_count{0};
  [[maybe_unused]] auto conn{
    r.on_destroy<health>().connect([&](entity_id const e, health const&) noexcept {
      ++fire_count;
      reentry_result = r.destroy(e);
    })
  };
  auto const a{r.create()};
  nexenne::utility::ignore(r.add<health>(a, health{.hp = 1}));

  CHECK(r.destroy(a));
  CHECK(fire_count == 1);
  CHECK_FALSE(reentry_result);
  CHECK(r.alive() == 0);

  auto const b{r.create()};
  auto const c{r.create()};
  CHECK(b.index() != c.index());
}

TEST_CASE("clear() fires on_destroy for every live component") {
  auto r{registry{}};
  auto pos_count{0};
  auto hp_count{0};
  [[maybe_unused]] auto c1{
    r.on_destroy<position>().connect([&](entity_id, position const&) noexcept { ++pos_count; })
  };
  [[maybe_unused]] auto c2{r.on_destroy<health>().connect([&](entity_id, health const&) noexcept {
    ++hp_count;
  })};

  auto const a{r.create()};
  auto const b{r.create()};
  nexenne::utility::ignore(r.add<position>(a, position{}));
  nexenne::utility::ignore(r.add<health>(a, health{.hp = 1}));
  nexenne::utility::ignore(r.add<position>(b, position{}));

  r.clear();

  CHECK(pos_count == 2);
  CHECK(hp_count == 1);
  CHECK(r.alive() == 0);
}

template <std::size_t N>
struct race_tag {};

TEST_CASE("type_id mints unique ids for types first-touched concurrently") {
  constexpr auto count{std::size_t{8}};
  auto ids{std::array<std::size_t, count>{}};
  auto threads{std::array<std::thread, count>{}};
  [&]<std::size_t... Is>(std::index_sequence<Is...>) noexcept {
    ((threads[Is] = std::thread{[&ids]() noexcept { ids[Is] = type_id<race_tag<Is>>(); }}), ...);
  }(std::make_index_sequence<count>{});
  for (auto& t : threads) {
    t.join();
  }
  std::ranges::sort(ids);
  CHECK(std::ranges::adjacent_find(ids) == ids.end());
}

TEST_CASE("registry: a listener calling clear() during destroy frees the index once") {
  auto r{registry{}};
  [[maybe_unused]] auto conn{
    r.on_destroy<tag_player>().connect([&r](entity_id, tag_player&) noexcept { r.clear(); })
  };
  auto const p{r.create()};
  nexenne::utility::ignore(r.add<tag_player>(p, tag_player{}));
  nexenne::utility::ignore(r.create(), r.create());

  CHECK(r.destroy(p));
  CHECK_FALSE(r.valid(p));
  CHECK(r.alive() == 0);

  auto made{std::vector<entity_id>{}};
  for (auto i{0}; i < 5; ++i) {
    made.push_back(r.create());
  }
  CHECK(r.alive() == 5);
  std::ranges::sort(made);
  CHECK(std::ranges::adjacent_find(made) == made.end());
}

TEST_CASE("registry: clear() marks every entity dead before firing on_destroy") {
  auto r{registry{}};
  auto hp_fires{0};
  auto pos_fires{0};
  auto seen_valid{true};
  auto nested_destroy{true};
  [[maybe_unused]] auto c1{
    r.on_destroy<health>().connect([&](entity_id const e, health const&) noexcept {
      ++hp_fires;
      seen_valid = r.valid(e);
      nested_destroy = r.destroy(e);
    })
  };
  [[maybe_unused]] auto c2{
    r.on_destroy<position>().connect([&](entity_id, position const&) noexcept { ++pos_fires; })
  };
  auto const a{r.create()};
  nexenne::utility::ignore(r.add<health>(a, health{.hp = 5}));
  nexenne::utility::ignore(r.add<position>(a, position{}));

  r.clear();
  CHECK_FALSE(seen_valid);
  CHECK_FALSE(nested_destroy);
  CHECK(hp_fires == 1);
  CHECK(pos_fires == 1);
  CHECK(r.alive() == 0);
}

struct label {
  std::string text{};
};

TEST_CASE("registry: clear() never hands a listener a component a nested destroy erased") {
  auto r{registry{}};
  [[maybe_unused]] auto c1{r.on_destroy<label>().connect([&r](entity_id const e, label&) noexcept {
    nexenne::utility::ignore(r.destroy(e));
  })};
  auto fires{0};
  auto total{std::size_t{0}};
  [[maybe_unused]] auto c2{r.on_destroy<label>().connect([&](entity_id, label& l) noexcept {
    ++fires;
    total += l.text.size();
  })};
  auto const a{r.create()};
  nexenne::utility::ignore(r.add<label>(a, label{std::string(64, 'x')}));

  r.clear();
  CHECK(fires == 1);
  CHECK(total == 64);
}

TEST_CASE("registry: clear() from a remove listener fires on_destroy once and remove succeeds") {
  auto r{registry{}};
  auto cleared{false};
  [[maybe_unused]] auto c1{r.on_destroy<label>().connect([&](entity_id, label&) noexcept {
    if (!cleared) {
      cleared = true;
      r.clear();
    }
  })};
  auto fires{0};
  auto total{std::size_t{0}};
  [[maybe_unused]] auto c2{r.on_destroy<label>().connect([&](entity_id, label& l) noexcept {
    ++fires;
    total += l.text.size();
  })};
  auto pos_fires{0};
  [[maybe_unused]] auto c3{
    r.on_destroy<position>().connect([&](entity_id, position const&) noexcept { ++pos_fires; })
  };
  auto const a{r.create()};
  auto const b{r.create()};
  nexenne::utility::ignore(r.add<label>(a, label{std::string(64, 'x')}));
  nexenne::utility::ignore(r.add<label>(b, label{std::string(8, 'y')}));
  nexenne::utility::ignore(r.add<position>(a, position{}));

  CHECK(r.remove<label>(a));
  CHECK(fires == 2);
  CHECK(total == 64 + 8);
  CHECK(pos_fires == 1);
  CHECK(r.alive() == 0);
  CHECK(r.storage<label>().empty());
}

TEST_CASE("view: a loop that re-reads end() stops at the captured slot count") {
  auto r{registry{}};
  for (auto i{0}; i < 2; ++i) {
    auto const e{r.create()};
    nexenne::utility::ignore(r.add<velocity>(e, velocity{}), r.add<position>(e, position{}));
  }
  auto v{r.view<velocity, position>()};
  auto steps{0};
  for (auto it{v.begin()}; it != v.end() && steps < 8; ++it) {
    ++steps;
    if (steps == 1) {
      nexenne::utility::ignore(r.add<velocity>(r.create(), velocity{}));
    }
  }
  CHECK(steps == 2);
  CHECK(v.begin() != v.end());
  CHECK(view<position>::iterator{} == view<position>::iterator{});
}

TEST_CASE("storage: a values() loop that re-reads end() stops at the captured count") {
  auto r{registry{}};
  nexenne::utility::ignore(r.add<health>(r.create(), health{.hp = 1}));
  auto range{r.storage<health>().values()};
  auto steps{0};
  for (auto it{range.begin()}; it != range.end() && steps < 8; ++it) {
    ++steps;
    if (steps == 1) {
      CHECK((*it).hp == 1);
      auto const t{r.create()};
      nexenne::utility::ignore(r.add<health>(t, health{.hp = 99}));
      nexenne::utility::ignore(r.remove<health>(t));
    }
  }
  CHECK(steps == 1);
}

TEST_CASE("view: clear() inside each ends the walk") {
  auto r{registry{}};
  for (auto i{0}; i < 3; ++i) {
    nexenne::utility::ignore(r.add<health>(r.create(), health{.hp = i}));
  }
  auto visits{0};
  r.view<health>().each([&](health const& h) noexcept {
    ++visits;
    if (h.hp == 0) {
      r.clear();
    }
  });
  CHECK(visits == 1);
  CHECK(r.alive() == 0);
}

TEST_CASE("view: clear() inside a range-for ends the walk") {
  auto r{registry{}};
  for (auto i{0}; i < 3; ++i) {
    nexenne::utility::ignore(r.add<health>(r.create(), health{.hp = i}));
  }
  auto visits{0};
  for ([[maybe_unused]] auto const [e, h] : r.view<health>()) {
    ++visits;
    r.clear();
  }
  CHECK(visits == 1);
}

TEST_CASE("storage: clear() inside a values() loop ends the walk") {
  auto r{registry{}};
  for (auto i{0}; i < 3; ++i) {
    nexenne::utility::ignore(r.add<health>(r.create(), health{.hp = i}));
  }
  auto visits{0};
  for ([[maybe_unused]] auto const& h : r.storage<health>().values()) {
    ++visits;
    r.clear();
  }
  CHECK(visits == 1);
}

TEST_CASE("registry: destroy the entities a range-for collected, not inside it") {
  auto r{registry{}};
  for (auto i{0}; i < 4; ++i) {
    nexenne::utility::ignore(r.create());
  }
  auto doomed{std::vector<entity_id>{}};
  for (auto const e : r) {
    doomed.push_back(e);
  }
  CHECK(doomed.size() == 4);
  for (auto const e : doomed) {
    CHECK(r.destroy(e));
  }
  CHECK(r.alive() == 0);
  CHECK(r.begin() == r.end());
}

TEST_CASE("registry: an on_destroy listener sees the entity dead under destroy and clear") {
  auto r{registry{}};
  auto seen_valid{false};
  auto sibling_found{false};
  [[maybe_unused]] auto conn{
    r.on_destroy<health>().connect([&](entity_id const e, health const&) noexcept {
      seen_valid = r.valid(e);
      sibling_found = r.get<position>(e).has_value();
    })
  };
  auto const spawn{[&r]() noexcept {
    auto const e{r.create()};
    nexenne::utility::ignore(r.add<position>(e, position{}), r.add<health>(e, health{.hp = 1}));
    return e;
  }};

  CHECK(r.remove<health>(spawn()));
  CHECK(seen_valid);
  CHECK(sibling_found);

  CHECK(r.destroy(spawn()));
  CHECK_FALSE(seen_valid);
  CHECK_FALSE(sibling_found);

  nexenne::utility::ignore(spawn());
  seen_valid = true;
  sibling_found = true;
  r.clear();
  CHECK_FALSE(seen_valid);
  CHECK_FALSE(sibling_found);
}

template <typename T>
concept addable_component =
  requires(registry& r, entity_id const e) { r.add<T>(e, std::declval<T>()); };

template <typename T>
concept lookupable_component = requires(registry const& r, entity_id const e) { r.has<T>(e); };

template <typename T>
concept fetchable_component = requires(registry& r, entity_id const e) { r.get<T>(e); };

template <typename T>
concept removable_component = requires(registry& r, entity_id const e) { r.remove<T>(e); };

template <typename T>
concept viewable_component = requires(registry& r) { r.view<T>(); };

template <typename T>
concept queryable_component = requires(registry& r) { r.query().with<T>(); };

template <typename T>
concept excludable_component = requires(registry& r) { r.query().with<health>().without<T>(); };

TEST_CASE("registry: cv-qualified component types are rejected") {
  static_assert(nexenne::ecs::component<position>);
  static_assert(!nexenne::ecs::component<position const>);
  static_assert(!nexenne::ecs::component<position volatile>);

  static_assert(addable_component<position>);
  static_assert(lookupable_component<position>);
  static_assert(fetchable_component<position>);
  static_assert(removable_component<position>);
  static_assert(viewable_component<position>);
  static_assert(queryable_component<position>);
  static_assert(excludable_component<position>);

  static_assert(!addable_component<position const>);
  static_assert(!lookupable_component<position const>);
  static_assert(!fetchable_component<position const>);
  static_assert(!removable_component<position const>);
  static_assert(!viewable_component<position const>);
  static_assert(!queryable_component<position const>);
  static_assert(!excludable_component<position const>);

  auto r{registry{}};
  auto const e{r.create()};
  nexenne::utility::ignore(r.add<position>(e, position{.x = 5.0F}));
  CHECK(r.has<position>(e));
}

TEST_CASE("view: the driver is the include with the fewest slots, not the fewest live") {
  auto r{registry{}};
  auto const m1{r.create()};
  auto const m2{r.create()};
  nexenne::utility::ignore(r.add<health>(m1, health{.hp = 1}));
  nexenne::utility::ignore(r.add<health>(m2, health{.hp = 2}));
  auto burst{std::vector<entity_id>{}};
  for (auto i{0}; i < 3; ++i) {
    burst.push_back(r.create());
    nexenne::utility::ignore(r.add<health>(burst.back(), health{}));
  }
  for (auto const e : burst) {
    nexenne::utility::ignore(r.remove<health>(e));
  }
  nexenne::utility::ignore(r.add<position>(m2, position{}));
  nexenne::utility::ignore(r.add<position>(m1, position{}));
  nexenne::utility::ignore(r.add<position>(r.create(), position{}));
  REQUIRE(r.storage<health>().size() < r.storage<position>().size());
  REQUIRE(r.storage<health>().slot_count() > r.storage<position>().slot_count());

  auto order{std::vector<entity_id>{}};
  r.view<health, position>().each([&order](entity_id const e, health&, position&) noexcept {
    order.push_back(e);
  });
  CHECK(order == std::vector<entity_id>{m2, m1});
}

TEST_CASE("storage: insert, erase and clear bypass validity and signals, as documented") {
  auto r{registry{}};
  auto constructed{0};
  auto destroyed{0};
  [[maybe_unused]] auto c1{r.on_construct<health>().connect([&](entity_id, health const&) noexcept {
    ++constructed;
  })};
  [[maybe_unused]] auto c2{r.on_destroy<health>().connect([&](entity_id, health const&) noexcept {
    ++destroyed;
  })};
  auto const a{r.create()};
  CHECK(r.destroy(a));

  CHECK(r.storage<health>().insert(a.index(), health{.hp = 13}));
  CHECK(constructed == 0);
  auto const b{r.create()};
  REQUIRE(b.index() == a.index());
  CHECK(r.has<health>(b));

  CHECK(r.storage<health>().erase(b.index()));
  nexenne::utility::ignore(r.add<health>(b, health{.hp = 1}));
  r.storage<health>().clear();
  CHECK(destroyed == 0);
  CHECK(r.valid(b));
}

template <typename Builder, typename T>
concept includable_for = requires(Builder const& b) { b.template with<T>(); };

template <typename Builder, typename T>
concept excludable_for = requires(Builder const& b) { b.template without<T>(); };

TEST_CASE("query builder: a type cannot be both required and excluded") {
  using fresh = decltype(std::declval<registry&>().query());
  using with_alpha = decltype(std::declval<fresh const&>().with<alpha>());
  using without_alpha = decltype(std::declval<fresh const&>().without<alpha>());
  static_assert(excludable_for<with_alpha, beta>);
  static_assert(includable_for<without_alpha, beta>);
  static_assert(!excludable_for<with_alpha, alpha>);
  static_assert(!includable_for<without_alpha, alpha>);

  auto r{registry{}};
  auto const a{r.create()};
  auto const b{r.create()};
  nexenne::utility::ignore(r.add<alpha>(a, alpha{}), r.add<alpha>(b, alpha{}));
  nexenne::utility::ignore(r.add<beta>(b, beta{}));
  auto visits{0};
  r.query().with<alpha>().without<beta>().each([&visits](alpha&) noexcept { ++visits; });
  CHECK(visits == 1);
}

// NOLINTBEGIN(performance-noexcept-move-constructor): the move must be able to throw
struct throwing_component {
  int v{0};

  throwing_component() = default;

  throwing_component(throwing_component&& other) : v{other.v} {}

  auto operator=(throwing_component&& other) -> throwing_component& {
    v = other.v;
    return *this;
  }
};

// NOLINTEND(performance-noexcept-move-constructor)

static_assert(!noexcept(std::declval<registry&>().add(entity_id{}, throwing_component{})));
static_assert(noexcept(std::declval<registry&>().add(entity_id{}, position{})));

inline constexpr auto throwing_mutator{[](position&) {}};
inline constexpr auto nothrow_mutator{[](position&) noexcept {}};
static_assert(!noexcept(std::declval<registry&>().patch<position>(entity_id{}, throwing_mutator)));
static_assert(noexcept(std::declval<registry&>().patch<position>(entity_id{}, nothrow_mutator)));

inline constexpr auto throwing_visit{[](entity_id) {}};
inline constexpr auto nothrow_visit{[](entity_id) noexcept {}};
static_assert(!noexcept(std::declval<registry const&>().each(throwing_visit)));
static_assert(noexcept(std::declval<registry const&>().each(nothrow_visit)));

using position_view = view<position>;
inline constexpr auto throwing_each{[](position&) {}};
inline constexpr auto nothrow_each{[](position&) noexcept {}};
inline constexpr auto throwing_each_with_id{[](entity_id, position&) {}};
inline constexpr auto nothrow_each_with_id{[](entity_id, position&) noexcept {}};
static_assert(!noexcept(std::declval<position_view const&>().each(throwing_each)));
static_assert(noexcept(std::declval<position_view const&>().each(nothrow_each)));
static_assert(!noexcept(std::declval<position_view const&>().each(throwing_each_with_id)));
static_assert(noexcept(std::declval<position_view const&>().each(nothrow_each_with_id)));
static_assert(!noexcept(std::declval<registry&>().query().with<position>().each(throwing_each)));
static_assert(noexcept(std::declval<registry&>().query().with<position>().each(nothrow_each)));

}  // namespace
