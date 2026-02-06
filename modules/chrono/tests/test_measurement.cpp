/**
 * @file
 * @brief Tests for nexenne::chrono scope_timer, frame_timer, rate_limiter, profiler.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <string>

#include <nexenne/chrono/frame_timer.hpp>
#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/profiler.hpp>
#include <nexenne/chrono/rate_limiter.hpp>
#include <nexenne/chrono/scope_timer.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::scope_timer fires the callback with the scope duration") {
  using clk = ch::basic_manual_clock<struct st_tag>;
  clk::reset();
  clk::duration captured{};
  auto cb{[&captured](auto d) { captured = d; }};
  {
    ch::scope_timer<decltype(cb), clk> t{cb};
    clk::advance(250ms);
  }  // dtor fires here
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(captured) == 250ms);
}

TEST_CASE("nexenne::chrono::frame_timer averages fps over a window") {
  using clk = ch::basic_manual_clock<struct ft_tag>;
  clk::reset();
  ch::frame_timer<clk, 4> ft;
  CHECK(ft.fps() == doctest::Approx(0.0));    // no frames yet
  CHECK(ft.tick() == clk::duration::zero());  // first tick: no delta
  for (int i{0}; i < 4; ++i) {
    clk::advance(10ms);  // 100 fps frames
    static_cast<void>(ft.tick());
  }
  CHECK(ft.fps() == doctest::Approx(100.0));
  CHECK(ft.frame_count() == 5);
  CHECK(ft.started());
}

TEST_CASE("nexenne::chrono::rate_limiter token bucket paces acquisitions") {
  using clk = ch::basic_manual_clock<struct rl_tag>;
  clk::reset();
  ch::rate_limiter<clk> rl{3.0, 10.0};  // capacity 3, 10 tokens/sec
  // starts full: a burst of 3 succeeds, the 4th fails
  CHECK(rl.try_acquire());
  CHECK(rl.try_acquire());
  CHECK(rl.try_acquire());
  CHECK_FALSE(rl.try_acquire());
  // 10/sec means one token every 100 ms
  clk::advance(100ms);
  CHECK(rl.try_acquire());
  CHECK_FALSE(rl.try_acquire());
  CHECK(rl.capacity() == doctest::Approx(3.0));
}

TEST_CASE("nexenne::chrono::profiler aggregates samples by name") {
  using clk = ch::basic_manual_clock<struct pr_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  prof.record("work", 10ms);
  prof.record("work", 30ms);
  prof.record("other", 5ms);
  CHECK(prof.size() == 2);
  CHECK(prof.contains("work"));
  auto const w{prof["work"]};
  CHECK(w.count == 2);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(w.mean()) == 20ms);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(w.min) == 10ms);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(w.max) == 30ms);
  prof.reset();  // zeroes stats but retains buckets so sinks stay valid
  CHECK(prof.size() == 2);
  CHECK(prof["work"].count == 0);
  prof.remove("work");
  prof.remove("other");
  CHECK(prof.empty());  // remove drops buckets entirely
}

}  // namespace
