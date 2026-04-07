/**
 * @file
 * @brief static_flat_map as a fixed inline config table: no heap, reports full.
 *
 * A four-entry map stored entirely inline; entries stay sorted by key, and once
 * it is full an insert of a new key returns container_error::full instead of
 * allocating or overflowing.
 */

#include <print>
#include <string_view>

#include <nexenne/container/static_flat_map.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::static_flat_map<std::string_view, int, 4> config;  // inline, no allocation
  nexenne::utility::discard(config.insert_or_assign("width", 800));
  nexenne::utility::discard(config.insert_or_assign("height", 600));
  nexenne::utility::discard(config.insert_or_assign("depth", 32));

  std::print("config (sorted by key):");
  for (auto const& [key, value] : config) {
    std::print(" {}={}", key, value);
  }
  std::println("");

  if (auto const* const w{config.at("width")}) {
    std::println("width = {}", *w);
  }

  nexenne::utility::discard(config.insert_or_assign("fps", 60));  // the fourth entry: full
  auto const fifth{config.insert_or_assign("vsync", 1)};          // a fifth new key
  std::println("adding a fifth key: {}", fifth.has_value() ? "ok" : "full");
  // config (sorted by key): depth=32 height=600 width=800
  // width = 800
  // adding a fifth key: full
  return 0;
}
