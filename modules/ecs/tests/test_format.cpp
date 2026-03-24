#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>

#include <nexenne/ecs/format.hpp>
#include <nexenne/ecs/registry.hpp>

namespace ecs = nexenne::ecs;

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
