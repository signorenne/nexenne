/**
 * @file
 * @brief Act on the live elements of a range, via nexenne::utility::for_each_non_null.
 */

#include <array>
#include <memory>
#include <print>
#include <string>
#include <vector>

#include <nexenne/utility/for_each_non_null.hpp>

namespace {

struct sink {
  std::string name;
  int writes{0};

  auto write(int const record) -> void {
    ++writes;
    std::println("{} <- {}", name, record);
  }

  // A no-argument member so we can pass a pointer to member as the callable.
  auto flush() -> void {
    std::println("{} flush ({} write(s))", name, writes);
  }
};

}  // namespace

auto main() -> int {
  sink a{"a"};
  sink b{"b"};
  sink c{"c"};

  // A slot table where some entries are empty (null). The callback only ever
  // sees a live sink, so it never checks for null and never dereferences a hand.
  std::array<sink*, 5> const sinks{&a, nullptr, &b, nullptr, &c};

  int const record{42};
  nexenne::utility::for_each_non_null(sinks, [&](sink& s) { s.write(record); });

  // A pointer to member is a valid callable: std::invoke turns &sink::flush into
  // s.flush() for each non-null element. Smart pointers work as the handle too.
  std::vector<std::shared_ptr<sink>> owned;
  owned.emplace_back(std::make_shared<sink>("x"));
  owned.emplace_back(nullptr);
  owned.emplace_back(std::make_shared<sink>("y"));
  nexenne::utility::for_each_non_null(owned, &sink::flush);

  return 0;
}
