/**
 * @file
 * @brief Dispatch a variant of UI events with nexenne::utility::overloaded.
 */

#include <print>
#include <string>
#include <variant>
#include <vector>

#include <nexenne/utility/overloaded.hpp>

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

auto describe(event const& e) -> std::string {
  return std::visit(
    nexenne::utility::overloaded{
      [](click const& c) { return std::format("click at ({}, {})", c.x, c.y); },
      [](key_press const& k) { return std::format("key '{}' pressed", k.key); },
      [](resize const& r) { return std::format("resized to {}x{}", r.width, r.height); },
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
  return 0;
}
