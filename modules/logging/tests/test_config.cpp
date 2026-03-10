/**
 * @file
 * @brief Tests for nexenne::logging compile-time configuration.
 */

#include <doctest/doctest.h>

#include <cstddef>

#include <nexenne/logging/config.hpp>

namespace {

namespace lg = nexenne::logging;

// Namespace-scope so the static members are legal (local classes cannot have
// static data members).
struct missing_async {
  static constexpr std::size_t queue_size = 8;
};

struct hand_rolled {
  static constexpr std::size_t queue_size = 16;
  static constexpr bool async = false;
};

TEST_CASE("nexenne::logging::config exposes its queue size and async flag") {
  using cfg = lg::config<256, true>;
  static_assert(cfg::queue_size == 256);
  static_assert(cfg::async == true);

  using sync_cfg = lg::config<1, false>;
  static_assert(sync_cfg::queue_size == 1);
  static_assert(sync_cfg::async == false);
}

TEST_CASE("nexenne::logging::default_config takes its values from the build macros") {
  static_assert(lg::default_config::queue_size == NEXENNE_LOG_QUEUE_SIZE);
  static_assert(lg::default_config::async == NEXENNE_LOG_ASYNC);
}

TEST_CASE("nexenne::logging::config_like accepts a config and rejects unrelated types") {
  static_assert(lg::config_like<lg::config<64, true>>);
  static_assert(lg::config_like<lg::default_config>);
  static_assert(!lg::config_like<int>);
  static_assert(!lg::config_like<missing_async>);
  static_assert(lg::config_like<hand_rolled>);
}

}  // namespace
