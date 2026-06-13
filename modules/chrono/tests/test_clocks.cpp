/**
 * @file
 * @brief Tests for nexenne::chrono tick_clock and manual_clock.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/tick_clock.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

// A backend with a settable tick source so tests are deterministic.
struct fake_backend {
  using rep = std::int64_t;
  using period = std::micro;
  static constexpr bool is_steady = true;
  static inline rep value{0};

  static auto ticks() noexcept -> rep {
    return value;
  }
};

using fake_clock = ch::tick_clock<fake_backend>;

static_assert(ch::steady_clock_like<fake_clock>);

TEST_CASE("nexenne::chrono::tick_clock wraps a backend as a chrono clock") {
  fake_backend::value = 1'500'000;  // 1.5 s worth of microseconds
  auto const tp{fake_clock::now()};
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) == 1500ms);
  CHECK(fake_clock::to_ticks(tp) == 1'500'000);
  CHECK(fake_clock::to_ticks(fake_clock::from_ticks(42)) == 42);
}

TEST_CASE("nexenne::chrono::manual_clock advances only when told to") {
  using clk = ch::basic_manual_clock<struct test_advance_tag>;
  clk::reset();
  CHECK(clk::now().time_since_epoch() == 0ns);
  clk::advance(100ms);
  CHECK(
    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()) == 100ms
  );
  clk::advance(50ms);
  CHECK(
    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()) == 150ms
  );
  clk::reset();
  CHECK(clk::now().time_since_epoch() == 0ns);
}

TEST_CASE("nexenne::chrono::manual_clock set and distinct tags are independent") {
  using a = ch::basic_manual_clock<struct tag_a>;
  using b = ch::basic_manual_clock<struct tag_b>;
  a::reset();
  b::reset();
  a::advance(1s);
  CHECK(a::now().time_since_epoch() != 0ns);
  CHECK(b::now().time_since_epoch() == 0ns);  // b unaffected by a

  b::set(b::time_point{2s});
  CHECK(std::chrono::duration_cast<std::chrono::seconds>(b::now().time_since_epoch()) == 2s);
}

}  // namespace
