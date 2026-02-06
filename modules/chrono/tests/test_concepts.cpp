/**
 * @file
 * @brief Tests for nexenne::chrono concepts.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>

#include <nexenne/chrono/concepts.hpp>

namespace {

namespace ch = nexenne::chrono;

struct micro_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady = true;

  static auto ticks() noexcept -> rep {
    return 0;
  }
};

struct not_steady_clock {
  using rep = int;
  using period = std::milli;
  using duration = std::chrono::milliseconds;
  using time_point = std::chrono::time_point<not_steady_clock>;
  static constexpr bool is_steady = false;

  static auto now() noexcept -> time_point {
    return time_point{};
  }
};

static_assert(ch::chrono_duration<std::chrono::milliseconds>);
static_assert(ch::chrono_duration<std::chrono::duration<double>>);
static_assert(!ch::chrono_duration<int>);

static_assert(ch::clock_like<std::chrono::steady_clock>);
static_assert(ch::clock_like<not_steady_clock>);
static_assert(!ch::clock_like<int>);

static_assert(ch::steady_clock_like<std::chrono::steady_clock>);
static_assert(!ch::steady_clock_like<not_steady_clock>);  // is_steady == false

static_assert(ch::tick_backend<micro_backend>);
static_assert(!ch::tick_backend<int>);

TEST_CASE("nexenne::chrono concepts are satisfied by the expected types") {
  // The static_asserts above carry the checks; this keeps the suite non-empty.
  CHECK(ch::chrono_duration<std::chrono::seconds>);
  CHECK(ch::steady_clock_like<std::chrono::steady_clock>);
  CHECK(ch::tick_backend<micro_backend>);
}

}  // namespace
