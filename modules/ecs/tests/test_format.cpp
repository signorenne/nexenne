/**
 * @file
 * @brief Tests for nexenne::ecs formatting (to_string, operator<<, std::formatter).
 */

#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>

#include <nexenne/ecs/format.hpp>
#include <nexenne/ecs/registry.hpp>
#include <nexenne/ecs/view.hpp>

namespace ecs = nexenne::ecs;

namespace {

struct position {
  int x;
};

struct velocity {
  int dx;
};

struct frozen {};

}  // namespace

TEST_CASE("to_string prints a live handle as entity(index, generation)") {
  ecs::registry reg{};
  auto const e{reg.create()};
  auto const expected{std::format("entity({}, {})", e.index(), e.generation())};
  CHECK(ecs::to_string(e) == expected);
}

TEST_CASE("the default (invalid) handle prints as entity(invalid)") {
  ecs::entity_id const invalid{};
  CHECK(ecs::to_string(invalid) == "entity(invalid)");
}

TEST_CASE("std::format and operator<< agree with to_string") {
  ecs::registry reg{};
  auto const e{reg.create()};

  CHECK(std::format("{}", e) == ecs::to_string(e));

  std::ostringstream os{};
  os << e;
  CHECK(os.str() == ecs::to_string(e));

  ecs::entity_id const invalid{};
  CHECK(std::format("{}", invalid) == "entity(invalid)");
}

TEST_CASE("the formatter honours width and alignment specs") {
  // Inheriting std::formatter<std::string_view> means a spec applies to the
  // whole rendering, so the handle right- and left-aligns like any string.
  ecs::entity_id const invalid{};
  CHECK(std::format("{:>20}", invalid) == "     entity(invalid)");
  CHECK(std::format("{:<20}", invalid) == "entity(invalid)     ");
}

TEST_CASE("a registry prints its live entity count") {
  ecs::registry reg{};
  CHECK(std::format("{}", reg) == "registry(alive=0)");
  auto const first{reg.create()};
  reg.create();
  CHECK(std::format("{}", reg) == "registry(alive=2)");
  reg.destroy(first);
  CHECK(std::format("{}", reg) == "registry(alive=1)");

  std::ostringstream os{};
  os << reg;
  CHECK(os.str() == ecs::to_string(reg));
}

TEST_CASE("a component_storage prints its live and slot counts") {
  ecs::registry reg{};
  auto const a{reg.create()};
  auto const b{reg.create()};
  reg.add(a, position{1});
  reg.add(b, position{2});
  auto const& storage{reg.storage<position>()};
  CHECK(std::format("{}", storage) == "component_storage(size=2, slots=2)");
  reg.remove<position>(a);
  CHECK(std::format("{}", storage) == "component_storage(size=1, slots=2)");

  std::ostringstream os{};
  os << storage;
  CHECK(os.str() == ecs::to_string(storage));
}

TEST_CASE("a query builder prints the sizes of its accumulated lists") {
  ecs::registry reg{};
  auto const empty{reg.query()};
  CHECK(std::format("{}", empty) == "query(with=0, without=0)");
  auto const q{reg.query().with<position>().with<velocity>().without<frozen>()};
  CHECK(std::format("{}", q) == "query(with=2, without=1)");

  std::ostringstream os{};
  os << q;
  CHECK(os.str() == ecs::to_string(q));
}
