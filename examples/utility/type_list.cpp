/**
 * @file
 * @brief A compile-time registry of message types for a tiny dispatcher.
 *
 * Keeps a list of the message structs a subsystem understands, in wire order,
 * then queries it purely at compile time. Only telemetry carries a payload, and
 * one stray message is deliberately left unregistered.
 *
 *   1. Count the messages, read one by index, and test membership.
 *   2. Derive a stable wire id as the index of a type within the list.
 *   3. Filter out the payload-free control messages, which take a fast
 *      dispatch path.
 *   4. Extend the list, and deduplicate a concatenation back to the original.
 */

#include <print>
#include <type_traits>

#include <nexenne/utility/type_list.hpp>

namespace util = nexenne::utility;

struct connect {};

struct ping {};

struct telemetry {
  double value{0.0};
};

struct disconnect {};

struct stray {};

using protocol = util::type_list<connect, ping, telemetry, disconnect>;

template <typename Msg>
inline constexpr auto wire_id_v{util::tl_index_of_v<protocol, Msg>};

auto main() -> int {
  static_assert(util::tl_size_v<protocol> == 4, "four registered messages");
  static_assert(std::is_same_v<util::tl_at_t<protocol, 0>, connect>);
  static_assert(util::tl_contains_v<protocol, ping>, "ping is registered");
  static_assert(!util::tl_contains_v<protocol, stray>, "stray is not registered");
  static_assert(wire_id_v<connect> == 0);
  static_assert(wire_id_v<disconnect> == 3);

  using control = util::tl_filter_t<protocol, std::is_empty>;
  static_assert(util::tl_size_v<control> == 3, "telemetry carries a payload");
  static_assert(util::tl_contains_v<control, ping>);
  static_assert(!util::tl_contains_v<control, telemetry>);

  using extended = util::tl_push_back_t<protocol, stray>;
  using deduped = util::tl_unique_t<util::tl_concat_t<protocol, protocol>>;
  static_assert(util::tl_size_v<extended> == 5);
  static_assert(util::tl_size_v<deduped> == 4, "duplicates collapse");

  std::println("protocol has {} messages", util::tl_size_v<protocol>);
  std::println("{} of them are payload-free control messages", util::tl_size_v<control>);
  std::println("wire id of disconnect is {}", wire_id_v<disconnect>);
  return 0;
}
