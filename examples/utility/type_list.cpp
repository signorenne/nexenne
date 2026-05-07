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

struct telemetry {
  double value{0.0};  // the one registered message that carries a payload
};

struct disconnect {};

struct stray {};  // deliberately not registered

// The protocol's registered messages, in wire order.
using protocol = util::type_list<connect, ping, telemetry, disconnect>;

// A stable wire id is just the index of the type within the protocol list.
template <typename Msg>
inline constexpr auto wire_id_v{util::tl_index_of_v<protocol, Msg>};

auto main() -> int {
  static_assert(util::tl_size_v<protocol> == 4, "four registered messages");
  static_assert(std::is_same_v<util::tl_at_t<protocol, 0>, connect>);
  static_assert(util::tl_contains_v<protocol, ping>, "ping is registered");
  static_assert(!util::tl_contains_v<protocol, stray>, "stray is not registered");
  static_assert(wire_id_v<connect> == 0);
  static_assert(wire_id_v<disconnect> == 3);

  // Filtering carves a subset out of the protocol at compile time: the empty
  // (payload-free) messages take a fast dispatch path.
  using control = util::tl_filter_t<protocol, std::is_empty>;
  static_assert(util::tl_size_v<control> == 3, "telemetry carries a payload");
  static_assert(util::tl_contains_v<control, ping>);
  static_assert(!util::tl_contains_v<control, telemetry>);

  // Extending and deduplicating the protocol are also compile-time operations.
  using extended = util::tl_push_back_t<protocol, stray>;
  using deduped = util::tl_unique_t<util::tl_concat_t<protocol, protocol>>;
  static_assert(util::tl_size_v<extended> == 5);
  static_assert(util::tl_size_v<deduped> == 4, "duplicates collapse");

  std::println("protocol has {} messages", util::tl_size_v<protocol>);
  std::println("{} of them are payload-free control messages", util::tl_size_v<control>);
  std::println("wire id of disconnect is {}", wire_id_v<disconnect>);
  return 0;
}
