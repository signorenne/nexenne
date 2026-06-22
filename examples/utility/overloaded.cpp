/**
 * @file
 * @brief Dispatch a variant of UI events with nexenne::utility::overloaded.
 *
 * overloaded bundles several lambdas into one callable whose overload set is the
 * union of theirs - the canonical std::visit visitor. Two patterns here:
 *   1. one typed lambda per alternative (exhaustive, missing branch = error);
 *   2. a few typed lambdas plus a generic `auto` catch-all (handle some, default
 *      the rest), where the typed overloads win by being more specialised.
 */

#include <print>
#include <string>
#include <variant>
#include <vector>

#include <nexenne/utility/overloaded.hpp>

namespace util = nexenne::utility;

namespace {

struct click {
  int x{0};
  int y{0};
};

struct key_press {
  char key{'\0'};
};

struct resize {
  int width{0};
  int height{0};
};

using event = std::variant<click, key_press, resize>;

// Pattern 1: one branch per alternative. Every type is handled explicitly; if a
// new alternative were added to `event`, this would fail to compile.
auto describe(event const& e) -> std::string {
  return std::visit(
    util::overloaded{
      [](click const& c) { return std::format("click at ({}, {})", c.x, c.y); },
      [](key_press const& k) { return std::format("key '{}' pressed", k.key); },
      [](resize const& r) { return std::format("resized to {}x{}", r.width, r.height); },
    },
    e
  );
}

// Pattern 2: handle the one alternative we care about and default the rest with
// a generic lambda. The typed `resize` overload is more specialised, so it wins
// for resize events; everything else falls through to the `auto` branch.
auto only_resizes(event const& e) -> std::string {
  return std::visit(
    util::overloaded{
      [](resize const& r) { return std::format("RESIZE {}x{}", r.width, r.height); },
      [](auto const&) { return std::string{"(ignored)"}; },
    },
    e
  );
}

}  // namespace

auto main() -> int {
  std::vector<event> const events{click{10, 20}, key_press{'q'}, resize{800, 600}};
  for (event const& e : events) {
    std::println("{}", describe(e));
  }
  // click at (10, 20)
  // key 'q' pressed
  // resized to 800x600

  std::println("--- only resizes ---");
  for (event const& e : events) {
    std::println("{}", only_resizes(e));
  }
  // (ignored)
  // (ignored)
  // RESIZE 800x600
  return 0;
}
