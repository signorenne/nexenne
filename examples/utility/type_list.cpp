/**
 * @file
 * @brief A compile-time registry of message types for a tiny dispatcher.
 *
 * Keeps a list of the message structs a subsystem understands, then queries it
 * purely at compile time: how many, the stable wire id of each, and whether a
 * candidate type is registered.
 */

#include <print>
#include <type_traits>

#include <nexenne/utility/type_list.hpp>

namespace util = nexenne::utility;

struct connect {};

struct ping {};

struct disconnect {};

struct stray {};  // deliberately not registered

// The protocol's registered messages, in wire order.
using protocol = util::type_list<connect, ping, disconnect>;

// A stable wire id is just the index of the type within the protocol list.
template <typename Msg>
inline constexpr auto wire_id_v{util::tl_index_of_v<protocol, Msg>};

auto main() -> int {
  static_assert(util::tl_size_v<protocol> == 3, "three registered messages");
  static_assert(std::is_same_v<util::tl_at_t<protocol, 0>, connect>);
  static_assert(util::tl_contains_v<protocol, ping>, "ping is registered");
  static_assert(!util::tl_contains_v<protocol, stray>, "stray is not registered");
  static_assert(wire_id_v<connect> == 0);
  static_assert(wire_id_v<disconnect> == 2);

  // Extending and deduplicating the protocol are also compile-time operations.
  using extended = util::tl_push_back_t<protocol, stray>;
  using deduped = util::tl_unique_t<util::tl_concat_t<protocol, protocol>>;
  static_assert(util::tl_size_v<extended> == 4);
  static_assert(util::tl_size_v<deduped> == 3, "duplicates collapse");

  std::println("protocol has {} messages", util::tl_size_v<protocol>);
  std::println("wire id of disconnect is {}", wire_id_v<disconnect>);
  return 0;
}
