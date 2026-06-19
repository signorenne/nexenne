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

using ms = std::chrono::milliseconds;

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

TEST_CASE("nexenne::chrono::scope_timer fires the callback exactly once") {
  using clk = ch::basic_manual_clock<struct st_once_tag>;
  clk::reset();
  int calls{0};
  clk::duration captured{};
  auto cb{[&](auto d) {
    ++calls;
    captured = d;
  }};
  {
    ch::scope_timer<decltype(cb), clk> t{cb};
    clk::advance(30ms);
    CHECK(calls == 0);  // not yet fired during the scope
  }
  CHECK(calls == 1);  // exactly once at destruction
  CHECK(std::chrono::duration_cast<ms>(captured) == 30ms);
}

TEST_CASE("nexenne::chrono::scope_timer reports zero for an empty scope") {
  using clk = ch::basic_manual_clock<struct st_empty_tag>;
  clk::reset();
  clk::duration captured{99ms};  // poison so we know the callback wrote it
  auto cb{[&](auto d) { captured = d; }};
  {
    ch::scope_timer<decltype(cb), clk> t{cb};
    // no advance
  }
  CHECK(captured == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::scope_timer elapsed reports mid-scope without firing") {
  using clk = ch::basic_manual_clock<struct st_mid_tag>;
  clk::reset();
  int calls{0};
  auto cb{[&](auto) { ++calls; }};
  {
    ch::scope_timer<decltype(cb), clk> t{cb};
    CHECK(t.elapsed() == clk::duration::zero());
    clk::advance(40ms);
    CHECK(std::chrono::duration_cast<ms>(t.elapsed()) == 40ms);
    CHECK(t.template elapsed<ms>() == 40ms);  // typed overload
    clk::advance(60ms);
    CHECK(std::chrono::duration_cast<ms>(t.elapsed()) == 100ms);
    CHECK(calls == 0);  // elapsed() never fires the callback
  }
  CHECK(calls == 1);
}

TEST_CASE("nexenne::chrono::scope_timer ignores time before construction") {
  using clk = ch::basic_manual_clock<struct st_before_tag>;
  clk::reset();
  clk::advance(500ms);  // wall time advances before the timer is built
  clk::duration captured{};
  auto cb{[&](auto d) { captured = d; }};
  {
    ch::scope_timer<decltype(cb), clk> t{cb};
    clk::advance(20ms);
  }
  CHECK(std::chrono::duration_cast<ms>(captured) == 20ms);  // only in-scope time
}

TEST_CASE("nexenne::chrono::scope_timer nested scopes time independently") {
  using clk = ch::basic_manual_clock<struct st_nested_tag>;
  clk::reset();
  clk::duration outer{};
  clk::duration inner{};
  auto outer_cb{[&](auto d) { outer = d; }};
  auto inner_cb{[&](auto d) { inner = d; }};
  {
    ch::scope_timer<decltype(outer_cb), clk> ot{outer_cb};
    clk::advance(10ms);
    {
      ch::scope_timer<decltype(inner_cb), clk> it{inner_cb};
      clk::advance(25ms);
    }  // inner fires: 25ms
    CHECK(std::chrono::duration_cast<ms>(inner) == 25ms);
    clk::advance(5ms);
  }  // outer fires: 10+25+5 = 40ms
  CHECK(std::chrono::duration_cast<ms>(outer) == 40ms);
}

TEST_CASE("nexenne::chrono::scope_timer ctad deduces the default steady clock") {
  // Deduction guide infers scope_timer<Callback> with the default clock.
  int calls{0};
  {
    ch::scope_timer t{[&](std::chrono::steady_clock::duration) { ++calls; }};
    static_cast<void>(t.elapsed());
  }
  CHECK(calls == 1);
}

TEST_CASE("nexenne::chrono::scope_timer feeds a profiler sink") {
  using clk = ch::basic_manual_clock<struct st_sink_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  for (int i : {10, 20, 30}) {
    auto t{ch::scope_timer<decltype(prof.sink("phase")), clk>{prof.sink("phase")}};
    clk::advance(ms{i});
  }
  auto const s{prof["phase"]};
  CHECK(s.count == 3);
  CHECK(std::chrono::duration_cast<ms>(s.total) == 60ms);
  CHECK(std::chrono::duration_cast<ms>(s.min) == 10ms);
  CHECK(std::chrono::duration_cast<ms>(s.max) == 30ms);
  CHECK(std::chrono::duration_cast<ms>(s.mean()) == 20ms);
}

TEST_CASE("nexenne::chrono::profiler default stats and unknown lookup") {
  using clk = ch::basic_manual_clock<struct pr_default_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  CHECK(prof.empty());
  CHECK(prof.size() == 0);
  CHECK_FALSE(prof.contains("nope"));
  auto const s{prof["nope"]};  // unknown: default stats, inserts nothing
  CHECK(s.count == 0);
  CHECK(s.mean() == clk::duration::zero());  // mean of empty is zero, not div-by-zero
  CHECK(prof.empty());                       // operator[] const did not insert
}

TEST_CASE("nexenne::chrono::profiler mean is zero for an empty bucket") {
  using clk = ch::basic_manual_clock<struct pr_mean0_tag>;
  ch::profiler<clk>::stats const s{};
  CHECK(s.count == 0);
  CHECK(s.mean() == clk::duration::zero());
  CHECK(s.min == clk::duration::max());  // sentinel before any sample
  CHECK(s.max == clk::duration::min());
}

TEST_CASE("nexenne::chrono::profiler single sample sets min equal to max") {
  using clk = ch::basic_manual_clock<struct pr_single_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  prof.record("x", 42ms);
  auto const s{prof["x"]};
  CHECK(s.count == 1);
  CHECK(std::chrono::duration_cast<ms>(s.total) == 42ms);
  CHECK(std::chrono::duration_cast<ms>(s.min) == 42ms);
  CHECK(std::chrono::duration_cast<ms>(s.max) == 42ms);
  CHECK(std::chrono::duration_cast<ms>(s.mean()) == 42ms);
}

TEST_CASE("nexenne::chrono::profiler sink caches the bucket across new insertions") {
  using clk = ch::basic_manual_clock<struct pr_sink_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  auto sink_a{prof.sink("a")};  // bucket exists after sink()
  CHECK(prof.contains("a"));
  CHECK(prof["a"].count == 0);
  sink_a(10ms);
  // inserting other buckets must not invalidate the cached pointer (map is stable)
  prof.record("b", 1ms);
  prof.record("c", 1ms);
  prof.record("d", 1ms);
  sink_a(30ms);
  auto const s{prof["a"]};
  CHECK(s.count == 2);
  CHECK(std::chrono::duration_cast<ms>(s.total) == 40ms);
  CHECK(std::chrono::duration_cast<ms>(s.min) == 10ms);
  CHECK(std::chrono::duration_cast<ms>(s.max) == 30ms);
}

TEST_CASE("nexenne::chrono::profiler record and sink target the same bucket") {
  using clk = ch::basic_manual_clock<struct pr_mix_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  prof.record("shared", 10ms);
  auto sink{prof.sink("shared")};  // same name: no new bucket
  CHECK(prof.size() == 1);
  sink(20ms);
  auto const s{prof["shared"]};
  CHECK(s.count == 2);
  CHECK(std::chrono::duration_cast<ms>(s.total) == 30ms);
}

TEST_CASE("nexenne::chrono::profiler handles negative samples via min/max sentinels") {
  using clk = ch::basic_manual_clock<struct pr_neg_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  prof.record("clock", -5ms);
  prof.record("clock", -20ms);
  auto const s{prof["clock"]};
  CHECK(s.count == 2);
  CHECK(std::chrono::duration_cast<ms>(s.min) == -20ms);
  CHECK(std::chrono::duration_cast<ms>(s.max) == -5ms);  // max() sentinel keeps negatives
  CHECK(std::chrono::duration_cast<ms>(s.total) == -25ms);
}

TEST_CASE("nexenne::chrono::profiler reset zeroes all buckets but keeps them") {
  using clk = ch::basic_manual_clock<struct pr_reset_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  prof.record("a", 10ms);
  prof.record("b", 20ms);
  prof.reset();
  CHECK(prof.size() == 2);  // buckets retained
  CHECK(prof["a"].count == 0);
  CHECK(prof["b"].count == 0);
  CHECK(prof["a"].min == clk::duration::max());  // sentinels restored
  CHECK(prof["a"].max == clk::duration::min());
  // a sink obtained before reset still records into the live bucket
  auto sink{prof.sink("a")};
  sink(7ms);
  CHECK(prof["a"].count == 1);
}

TEST_CASE("nexenne::chrono::profiler named reset clears only one bucket") {
  using clk = ch::basic_manual_clock<struct pr_namedreset_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  prof.record("a", 10ms);
  prof.record("b", 20ms);
  prof.reset("a");
  CHECK(prof["a"].count == 0);
  CHECK(prof["b"].count == 1);  // untouched
  prof.reset("ghost");          // unknown: no-op, no insert
  CHECK(prof.size() == 2);
}

TEST_CASE("nexenne::chrono::profiler remove drops a bucket and unknown remove is a no-op") {
  using clk = ch::basic_manual_clock<struct pr_remove_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  prof.record("a", 1ms);
  prof.record("b", 1ms);
  prof.remove("ghost");  // no-op
  CHECK(prof.size() == 2);
  prof.remove("a");
  CHECK(prof.size() == 1);
  CHECK_FALSE(prof.contains("a"));
  CHECK(prof.contains("b"));
}

TEST_CASE("nexenne::chrono::profiler buckets view exposes ordered names") {
  using clk = ch::basic_manual_clock<struct pr_buckets_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  prof.record("zeta", 1ms);
  prof.record("alpha", 2ms);
  prof.record("mu", 3ms);
  auto const& b{prof.buckets()};
  REQUIRE(b.size() == 3);
  // std::map orders keys: alpha < mu < zeta
  auto it{b.begin()};
  CHECK(it->first == "alpha");
  ++it;
  CHECK(it->first == "mu");
  ++it;
  CHECK(it->first == "zeta");
}

TEST_CASE("nexenne::chrono::profiler heterogeneous lookup avoids string churn") {
  using clk = ch::basic_manual_clock<struct pr_het_tag>;
  clk::reset();
  ch::profiler<clk> prof;
  std::string const name{"section"};
  prof.record(std::string_view{name}, 5ms);
  CHECK(prof.contains(std::string_view{name}));
  CHECK(prof["section"].count == 1);  // string literal lookup hits the same bucket
}

TEST_CASE("nexenne::chrono::frame_timer is unstarted at construction") {
  using clk = ch::basic_manual_clock<struct ft_ctor_tag>;
  clk::reset();
  ch::frame_timer<clk, 8> ft;
  CHECK_FALSE(ft.started());
  CHECK(ft.frame_count() == 0);
  CHECK(ft.fps() == doctest::Approx(0.0));
}

TEST_CASE("nexenne::chrono::frame_timer first tick yields zero delta and no fps") {
  using clk = ch::basic_manual_clock<struct ft_first_tag>;
  clk::reset();
  ch::frame_timer<clk, 4> ft;
  auto const d{ft.tick()};
  CHECK(d == clk::duration::zero());  // first frame has no predecessor
  CHECK(ft.started());
  CHECK(ft.frame_count() == 1);
  CHECK(ft.fps() == doctest::Approx(0.0));  // window still empty
  CHECK(ft.last_tick() == clk::now());
}

TEST_CASE("nexenne::chrono::frame_timer fixed step gives that exact delta") {
  using clk = ch::basic_manual_clock<struct ft_step_tag>;
  clk::reset();
  ch::frame_timer<clk, 4> ft;
  static_cast<void>(ft.tick());  // prime
  clk::advance(16ms);
  CHECK(std::chrono::duration_cast<ms>(ft.tick()) == 16ms);
  clk::advance(33ms);
  CHECK(std::chrono::duration_cast<ms>(ft.tick()) == 33ms);
  CHECK(ft.frame_count() == 3);
}

TEST_CASE("nexenne::chrono::frame_timer fps reflects the average frame time") {
  using clk = ch::basic_manual_clock<struct ft_fps_tag>;
  clk::reset();
  ch::frame_timer<clk, 8> ft;
  static_cast<void>(ft.tick());  // prime
  // two frames: 10ms and 30ms -> mean 20ms -> 50 fps
  clk::advance(10ms);
  static_cast<void>(ft.tick());
  clk::advance(30ms);
  static_cast<void>(ft.tick());
  CHECK(ft.fps() == doctest::Approx(50.0));
}

TEST_CASE("nexenne::chrono::frame_timer window evicts old frames") {
  using clk = ch::basic_manual_clock<struct ft_window_tag>;
  clk::reset();
  ch::frame_timer<clk, 2> ft;  // only the last 2 deltas matter for fps
  static_cast<void>(ft.tick());
  // fill the window with two slow 100ms frames (10 fps)
  clk::advance(100ms);
  static_cast<void>(ft.tick());
  clk::advance(100ms);
  static_cast<void>(ft.tick());
  CHECK(ft.fps() == doctest::Approx(10.0));
  // now two fast 10ms frames push the slow ones out (100 fps)
  clk::advance(10ms);
  static_cast<void>(ft.tick());
  clk::advance(10ms);
  static_cast<void>(ft.tick());
  CHECK(ft.fps() == doctest::Approx(100.0));
}

TEST_CASE("nexenne::chrono::frame_timer survives a long-pause frame") {
  using clk = ch::basic_manual_clock<struct ft_pause_tag>;
  clk::reset();
  ch::frame_timer<clk, 2> ft;
  static_cast<void>(ft.tick());
  clk::advance(10ms);
  static_cast<void>(ft.tick());
  // a single huge 2-second hitch
  clk::advance(2s);
  auto const d{ft.tick()};
  CHECK(std::chrono::duration_cast<ms>(d) == 2000ms);
  // window now holds 10ms and 2000ms -> mean 1005ms -> ~0.995 fps
  CHECK(ft.fps() == doctest::Approx(1000.0 / 1005.0));
}

TEST_CASE("nexenne::chrono::frame_timer reset returns to the unstarted state") {
  using clk = ch::basic_manual_clock<struct ft_reset_tag>;
  clk::reset();
  ch::frame_timer<clk, 4> ft;
  for (int i{0}; i < 3; ++i) {
    clk::advance(10ms);
    static_cast<void>(ft.tick());
  }
  CHECK(ft.frame_count() == 3);
  ft.reset();
  CHECK_FALSE(ft.started());
  CHECK(ft.frame_count() == 0);
  CHECK(ft.fps() == doctest::Approx(0.0));
  // a fresh first tick after reset is once again a zero delta
  CHECK(ft.tick() == clk::duration::zero());
  CHECK(ft.frame_count() == 1);
}

TEST_CASE("nexenne::chrono::frame_timer fps stays zero when frames have no duration") {
  using clk = ch::basic_manual_clock<struct ft_zerodur_tag>;
  clk::reset();
  ch::frame_timer<clk, 4> ft;
  static_cast<void>(ft.tick());
  static_cast<void>(ft.tick());             // zero-delta frame
  static_cast<void>(ft.tick());             // another zero-delta frame
  CHECK(ft.fps() == doctest::Approx(0.0));  // sum_ns <= 0 guard kicks in
  CHECK(ft.frame_count() == 3);
}

TEST_CASE("nexenne::chrono::frame_timer window-size-one tracks only the latest frame") {
  using clk = ch::basic_manual_clock<struct ft_one_tag>;
  clk::reset();
  ch::frame_timer<clk, 1> ft;
  static_cast<void>(ft.tick());
  clk::advance(50ms);
  static_cast<void>(ft.tick());  // 20 fps
  CHECK(ft.fps() == doctest::Approx(20.0));
  clk::advance(20ms);
  static_cast<void>(ft.tick());  // window holds only this 20ms -> 50 fps
  CHECK(ft.fps() == doctest::Approx(50.0));
}

}  // namespace
