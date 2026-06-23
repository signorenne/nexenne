/**
 * @file
 * @brief Tests for nexenne::chrono stopwatch and static_stopwatch over manual_clock.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <format>
#include <optional>
#include <vector>

#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/static_stopwatch.hpp>
#include <nexenne/chrono/stopwatch.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

using ms = std::chrono::milliseconds;

TEST_CASE("nexenne::chrono::stopwatch accumulates across pause and resume") {
  using clk = ch::basic_manual_clock<struct sw_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  CHECK(sw.is_idle());
  sw.start();
  CHECK(sw.is_running());
  clk::advance(100ms);
  sw.pause();
  CHECK(sw.is_paused());
  CHECK(sw.template elapsed<std::chrono::milliseconds>() == 100ms);
  clk::advance(500ms);  // time passes while paused: not counted
  CHECK(sw.template elapsed<std::chrono::milliseconds>() == 100ms);
  sw.resume();
  clk::advance(50ms);
  CHECK(sw.template elapsed<std::chrono::milliseconds>() == 150ms);
}

TEST_CASE("nexenne::chrono::stopwatch records laps") {
  using clk = ch::basic_manual_clock<struct sw_lap_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(10ms);
  auto const l1{sw.lap()};
  REQUIRE(l1.has_value());
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(*l1) == 10ms);
  clk::advance(30ms);
  auto const l2{sw.lap()};
  REQUIRE(l2.has_value());
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(*l2) == 30ms);
  CHECK(sw.lap_count() == 2);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(sw.lap_sum()) == 40ms);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(*sw.lap_min()) == 10ms);
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(*sw.lap_max()) == 30ms);
}

TEST_CASE("nexenne::chrono::stopwatch std::format renders elapsed time") {
  using clk = ch::basic_manual_clock<struct sw_fmt_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(65s);
  CHECK(std::format("{}", sw) == "01m:05s");
}

TEST_CASE("nexenne::chrono::static_stopwatch bounds its lap buffer and counts drops") {
  using clk = ch::basic_manual_clock<struct fsw_tag>;
  clk::reset();
  ch::static_stopwatch<2, clk> sw;  // room for 2 laps (capacity first, clock second)
  sw.start();
  for (int i{0}; i < 4; ++i) {
    clk::advance(10ms);
    nexenne::utility::discard(sw.lap());
  }
  CHECK(sw.lap_count() == 4);         // total laps seen
  CHECK(sw.stored_lap_count() == 2);  // only the buffer capacity is retained
  CHECK(sw.laps_dropped() == 2);      // the overflow was counted, not lost silently
}

TEST_CASE("nexenne::chrono::stopwatch is idle with zero elapsed at construction") {
  using clk = ch::basic_manual_clock<struct sw_ctor_tag>;
  clk::reset();
  clk::advance(999ms);  // wall time moving before construction must not matter
  ch::stopwatch<clk> const sw;
  CHECK(sw.is_idle());
  CHECK_FALSE(sw.is_running());
  CHECK_FALSE(sw.is_paused());
  CHECK(sw.current_state() == ch::stopwatch<clk>::state::idle);
  CHECK(sw.elapsed() == clk::duration::zero());
  CHECK(sw.template elapsed<ms>() == 0ms);
  CHECK(sw.lap_count() == 0);
  CHECK(sw.lap_sum() == clk::duration::zero());
  CHECK_FALSE(sw.lap_min().has_value());
  CHECK_FALSE(sw.lap_max().has_value());
  CHECK_FALSE(sw.lap_average().has_value());
  CHECK_FALSE(sw.peek_lap().has_value());
  CHECK(sw.laps().empty());
}

TEST_CASE("nexenne::chrono::stopwatch elapsed is zero immediately after start") {
  using clk = ch::basic_manual_clock<struct sw_zero_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  CHECK(sw.is_running());
  CHECK(sw.elapsed() == clk::duration::zero());  // no advance yet
  CHECK(sw.template elapsed<ms>() == 0ms);
}

TEST_CASE("nexenne::chrono::stopwatch advancing yields the exact elapsed") {
  using clk = ch::basic_manual_clock<struct sw_exact_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(1234567ns);
  CHECK(sw.elapsed() == 1234567ns);
  CHECK(sw.template elapsed<std::chrono::microseconds>() == 1234us);  // truncating cast
}

TEST_CASE("nexenne::chrono::stopwatch start is a no-op while running or paused") {
  using clk = ch::basic_manual_clock<struct sw_nostart_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(100ms);
  sw.start();  // no-op: must NOT reset the accumulated 100ms
  CHECK(sw.is_running());
  CHECK(sw.template elapsed<ms>() == 100ms);
  sw.pause();
  sw.start();  // no-op while paused
  CHECK(sw.is_paused());
  CHECK(sw.template elapsed<ms>() == 100ms);
}

TEST_CASE("nexenne::chrono::stopwatch pause is a no-op unless running") {
  using clk = ch::basic_manual_clock<struct sw_nopause_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.pause();  // no-op while idle
  CHECK(sw.is_idle());
  sw.start();
  clk::advance(40ms);
  sw.pause();
  CHECK(sw.is_paused());
  CHECK(sw.template elapsed<ms>() == 40ms);
  clk::advance(10ms);
  sw.pause();  // no-op while already paused: must not fold the extra 10ms
  CHECK(sw.is_paused());
  CHECK(sw.template elapsed<ms>() == 40ms);
}

TEST_CASE("nexenne::chrono::stopwatch resume is a no-op unless paused") {
  using clk = ch::basic_manual_clock<struct sw_noresume_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.resume();  // no-op while idle
  CHECK(sw.is_idle());
  sw.start();
  sw.resume();  // no-op while running
  CHECK(sw.is_running());
  clk::advance(25ms);
  CHECK(sw.template elapsed<ms>() == 25ms);
}

TEST_CASE("nexenne::chrono::stopwatch accumulates across many pause/resume cycles") {
  using clk = ch::basic_manual_clock<struct sw_multi_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  for (int i{0}; i < 5; ++i) {
    clk::advance(20ms);  // counted
    sw.pause();
    clk::advance(1000ms);  // ignored
    sw.resume();
  }
  clk::advance(20ms);  // counted, sixth segment
  CHECK(sw.template elapsed<ms>() == 120ms);
}

TEST_CASE("nexenne::chrono::stopwatch reset returns to idle and clears laps") {
  using clk = ch::basic_manual_clock<struct sw_reset_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(30ms);
  nexenne::utility::discard(sw.lap());
  sw.reset();
  CHECK(sw.is_idle());
  CHECK(sw.elapsed() == clk::duration::zero());
  CHECK(sw.lap_count() == 0);
  CHECK(sw.lap_sum() == clk::duration::zero());
  CHECK(sw.laps().empty());
  clk::advance(500ms);  // idle: still zero
  CHECK(sw.elapsed() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::stopwatch restart clears prior state and re-times") {
  using clk = ch::basic_manual_clock<struct sw_restart_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(80ms);
  nexenne::utility::discard(sw.lap());
  nexenne::utility::discard(sw.lap());
  sw.restart();  // reset + start
  CHECK(sw.is_running());
  CHECK(sw.lap_count() == 0);
  CHECK(sw.elapsed() == clk::duration::zero());
  clk::advance(15ms);
  CHECK(sw.template elapsed<ms>() == 15ms);
}

TEST_CASE("nexenne::chrono::stopwatch start clears accumulated time after reset") {
  using clk = ch::basic_manual_clock<struct sw_startclear_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(50ms);
  sw.reset();
  sw.start();  // fresh start from idle
  clk::advance(7ms);
  CHECK(sw.template elapsed<ms>() == 7ms);
}

TEST_CASE("nexenne::chrono::stopwatch lap returns nullopt while idle") {
  using clk = ch::basic_manual_clock<struct sw_lapidle_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  CHECK_FALSE(sw.lap().has_value());
  CHECK(sw.lap_count() == 0);
  // typed overload also nullopt while idle
  CHECK_FALSE(sw.template lap<ms>().has_value());
}

TEST_CASE("nexenne::chrono::stopwatch typed lap casts the segment") {
  using clk = ch::basic_manual_clock<struct sw_laptyped_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(2500us);
  auto const l{sw.template lap<ms>()};
  REQUIRE(l.has_value());
  CHECK(*l == 2ms);  // 2500us truncates to 2ms
  CHECK(sw.lap_count() == 1);
}

TEST_CASE("nexenne::chrono::stopwatch first lap measures from start") {
  using clk = ch::basic_manual_clock<struct sw_firstlap_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  clk::advance(300ms);  // before start: not counted
  sw.start();
  clk::advance(45ms);
  auto const l{sw.lap()};
  REQUIRE(l.has_value());
  CHECK(std::chrono::duration_cast<ms>(*l) == 45ms);
}

TEST_CASE("nexenne::chrono::stopwatch peek_lap does not close the segment") {
  using clk = ch::basic_manual_clock<struct sw_peek_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(10ms);
  auto const p1{sw.peek_lap()};
  REQUIRE(p1.has_value());
  CHECK(std::chrono::duration_cast<ms>(*p1) == 10ms);
  CHECK(sw.lap_count() == 0);  // peek does not record
  clk::advance(5ms);
  auto const p2{sw.peek_lap()};
  REQUIRE(p2.has_value());
  CHECK(std::chrono::duration_cast<ms>(*p2) == 15ms);  // still measures from start
  CHECK(sw.lap_count() == 0);
  auto const l{sw.lap()};
  REQUIRE(l.has_value());
  CHECK(std::chrono::duration_cast<ms>(*l) == 15ms);  // closes from baseline (start)
}

TEST_CASE("nexenne::chrono::stopwatch peek_lap measures from the last lap baseline") {
  using clk = ch::basic_manual_clock<struct sw_peekbase_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(10ms);
  nexenne::utility::discard(sw.lap());  // baseline now at 10ms
  clk::advance(7ms);
  auto const p{sw.peek_lap()};
  REQUIRE(p.has_value());
  CHECK(std::chrono::duration_cast<ms>(*p) == 7ms);  // measured from baseline, not start
}

TEST_CASE("nexenne::chrono::stopwatch many laps aggregate correctly") {
  using clk = ch::basic_manual_clock<struct sw_manylaps_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.reserve_laps(8);  // pre-allocate; subsequent laps cannot throw
  sw.start();
  std::vector<int> const segs{5, 50, 20, 35, 10};
  for (auto const s : segs) {
    clk::advance(ms{s});
    nexenne::utility::discard(sw.lap());
  }
  CHECK(sw.lap_count() == 5);
  CHECK(std::chrono::duration_cast<ms>(sw.lap_sum()) == 120ms);
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_min()) == 5ms);
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_max()) == 50ms);
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_average()) == 24ms);  // 120/5
  REQUIRE(sw.laps().size() == 5);
  CHECK(std::chrono::duration_cast<ms>(sw.laps()[0]) == 5ms);
  CHECK(std::chrono::duration_cast<ms>(sw.laps()[3]) == 35ms);
}

TEST_CASE("nexenne::chrono::stopwatch lap continues to track elapsed across pause") {
  using clk = ch::basic_manual_clock<struct sw_lappause_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(10ms);
  sw.pause();
  clk::advance(999ms);  // ignored
  sw.resume();
  clk::advance(20ms);
  auto const l{sw.lap()};  // segment spans the pause: counted time only
  REQUIRE(l.has_value());
  CHECK(std::chrono::duration_cast<ms>(*l) == 30ms);
}

TEST_CASE("nexenne::chrono::stopwatch lap while paused returns frozen segment") {
  using clk = ch::basic_manual_clock<struct sw_lapfrozen_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(40ms);
  sw.pause();
  auto const l{sw.lap()};  // allowed while paused; uses frozen elapsed
  REQUIRE(l.has_value());
  CHECK(std::chrono::duration_cast<ms>(*l) == 40ms);
  CHECK(sw.lap_count() == 1);
  clk::advance(123ms);  // paused: peek stays frozen at remaining segment (zero)
  auto const p{sw.peek_lap()};
  REQUIRE(p.has_value());
  CHECK(*p == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::stopwatch clear_laps drops laps and rebases") {
  using clk = ch::basic_manual_clock<struct sw_clearlaps_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(10ms);
  nexenne::utility::discard(sw.lap());
  clk::advance(20ms);
  nexenne::utility::discard(sw.lap());
  CHECK(sw.lap_count() == 2);
  sw.clear_laps();
  CHECK(sw.lap_count() == 0);
  CHECK(sw.lap_sum() == clk::duration::zero());
  CHECK_FALSE(sw.lap_min().has_value());
  CHECK(sw.is_running());  // state preserved
  // next lap measures from the rebase point, not from start
  clk::advance(5ms);
  auto const l{sw.lap()};
  REQUIRE(l.has_value());
  CHECK(std::chrono::duration_cast<ms>(*l) == 5ms);
  // total elapsed is preserved across clear_laps
  CHECK(std::chrono::duration_cast<ms>(sw.elapsed()) == 35ms);
}

TEST_CASE("nexenne::chrono::stopwatch single lap min equals max equals average") {
  using clk = ch::basic_manual_clock<struct sw_single_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(17ms);
  nexenne::utility::discard(sw.lap());
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_min()) == 17ms);
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_max()) == 17ms);
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_average()) == 17ms);
}

TEST_CASE("nexenne::chrono::stopwatch zero-duration lap is recorded") {
  using clk = ch::basic_manual_clock<struct sw_zerolap_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  auto const l{sw.lap()};  // no advance: a real zero-length lap
  REQUIRE(l.has_value());
  CHECK(*l == clk::duration::zero());
  CHECK(sw.lap_count() == 1);
  CHECK(sw.lap_sum() == clk::duration::zero());
}

TEST_CASE("nexenne::chrono::stopwatch elapsed_at honours an explicit snapshot") {
  using clk = ch::basic_manual_clock<struct sw_elapsedat_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  auto const start_now{clk::now()};
  clk::advance(60ms);
  // elapsed_at against the start instant is zero even though now() moved
  CHECK(sw.elapsed_at(start_now) == clk::duration::zero());
  CHECK(std::chrono::duration_cast<ms>(sw.elapsed_at(clk::now())) == 60ms);
}

TEST_CASE("nexenne::chrono::stopwatch elapsed_at is frozen while paused and zero while idle") {
  using clk = ch::basic_manual_clock<struct sw_elapsedat2_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  CHECK(sw.elapsed_at(clk::now() + 1000ms) == clk::duration::zero());  // idle ignores now
  sw.start();
  clk::advance(30ms);
  sw.pause();
  CHECK(std::chrono::duration_cast<ms>(sw.elapsed_at(clk::now() + 5000ms)) == 30ms);  // frozen
}

TEST_CASE("nexenne::chrono::stopwatch comparison operators order by elapsed") {
  using clk = ch::basic_manual_clock<struct sw_cmp_tag>;
  clk::reset();
  ch::stopwatch<clk> a;
  ch::stopwatch<clk> b;
  a.start();
  clk::advance(50ms);
  b.start();  // b starts 50ms later, so it accrues less
  clk::advance(20ms);
  CHECK(a > b);
  CHECK(b < a);
  CHECK_FALSE(a == b);
  CHECK(a != b);
  // pause both, then equalize
  a.pause();  // a == 70ms
  b.pause();  // b == 20ms
  b.resume();
  clk::advance(50ms);  // b now 70ms
  b.pause();
  CHECK(a == b);
  CHECK_FALSE(a < b);
  CHECK_FALSE(a > b);
  CHECK((a <=> b) == std::strong_ordering::equal);
}

TEST_CASE("nexenne::chrono::stopwatch formatter suppress-zero off via '!' flag") {
  using clk = ch::basic_manual_clock<struct sw_fmtbang_tag>;
  clk::reset();
  ch::stopwatch<clk> sw;
  sw.start();
  clk::advance(65s);
  auto const def{std::format("{}", sw)};
  auto const bang{std::format("{:!}", sw)};
  CHECK(def == "01m:05s");
  CHECK(bang != def);                          // '!' changes the rendering
  CHECK(bang.find('d') != std::string::npos);  // includes the zeroed day field
}

TEST_CASE("nexenne::chrono::stopwatch formatter renders zero elapsed") {
  using clk = ch::basic_manual_clock<struct sw_fmtzero_tag>;
  clk::reset();
  ch::stopwatch<clk> const sw;  // idle, zero elapsed
  CHECK(std::format("{}", sw) == "00s");
}

static_assert(ch::static_stopwatch<4>::capacity == 4);
static_assert(ch::static_stopwatch<1>::capacity == 1);

TEST_CASE("nexenne::chrono::static_stopwatch capacity and constexpr state") {
  using clk = ch::basic_manual_clock<struct fsw_const_tag>;
  constexpr ch::static_stopwatch<3, clk> sw;
  static_assert(sw.capacity == 3);
  static_assert(sw.is_idle());
  static_assert(!sw.is_running());
  static_assert(!sw.is_paused());
  static_assert(sw.stored_lap_count() == 0);
  static_assert(sw.lap_count() == 0);
  static_assert(sw.laps_dropped() == 0);
  static_assert(sw.current_state() == ch::static_stopwatch<3, clk>::state::idle);
  CHECK(sw.is_idle());
}

TEST_CASE("nexenne::chrono::static_stopwatch mirrors stopwatch elapsed semantics") {
  using clk = ch::basic_manual_clock<struct fsw_parity_tag>;
  clk::reset();
  ch::static_stopwatch<4, clk> fsw;
  ch::stopwatch<clk> sw;
  fsw.start();
  sw.start();
  CHECK(fsw.elapsed() == sw.elapsed());  // both zero
  clk::advance(100ms);
  fsw.pause();
  sw.pause();
  CHECK(fsw.elapsed() == sw.elapsed());
  CHECK(fsw.template elapsed<ms>() == 100ms);
  clk::advance(500ms);  // paused: ignored by both
  CHECK(fsw.elapsed() == sw.elapsed());
  fsw.resume();
  sw.resume();
  clk::advance(50ms);
  CHECK(fsw.elapsed() == sw.elapsed());
  CHECK(fsw.template elapsed<ms>() == 150ms);
}

TEST_CASE("nexenne::chrono::static_stopwatch and stopwatch agree on lap stats below capacity") {
  using clk = ch::basic_manual_clock<struct fsw_agree_tag>;
  clk::reset();
  ch::static_stopwatch<8, clk> fsw;
  ch::stopwatch<clk> sw;
  fsw.start();
  sw.start();
  for (int s : {5, 50, 20, 35, 10}) {
    clk::advance(ms{s});
    nexenne::utility::discard(fsw.lap());
    nexenne::utility::discard(sw.lap());
  }
  CHECK(fsw.lap_count() == sw.lap_count());
  CHECK(fsw.stored_lap_count() == sw.lap_count());
  CHECK(fsw.laps_dropped() == 0);
  CHECK(fsw.lap_sum() == sw.lap_sum());
  CHECK(*fsw.lap_min() == *sw.lap_min());
  CHECK(*fsw.lap_max() == *sw.lap_max());
  CHECK(*fsw.lap_average() == *sw.lap_average());
  REQUIRE(fsw.laps().size() == 5);
  CHECK(std::chrono::duration_cast<ms>(fsw.laps()[1]) == 50ms);
}

TEST_CASE("nexenne::chrono::static_stopwatch idle accessors return empty") {
  using clk = ch::basic_manual_clock<struct fsw_idle_tag>;
  clk::reset();
  ch::static_stopwatch<3, clk> sw;
  CHECK_FALSE(sw.lap().has_value());
  CHECK_FALSE(sw.peek_lap().has_value());
  CHECK_FALSE(sw.lap_min().has_value());
  CHECK_FALSE(sw.lap_max().has_value());
  CHECK_FALSE(sw.lap_average().has_value());
  CHECK(sw.laps().empty());
  CHECK(sw.lap_count() == 0);
}

TEST_CASE("nexenne::chrono::static_stopwatch overflow keeps running mean faithful") {
  using clk = ch::basic_manual_clock<struct fsw_mean_tag>;
  clk::reset();
  ch::static_stopwatch<2, clk> sw;
  sw.start();
  // four laps of 10,20,30,40 ms; buffer holds only the first two
  for (int s : {10, 20, 30, 40}) {
    clk::advance(ms{s});
    nexenne::utility::discard(sw.lap());
  }
  CHECK(sw.lap_count() == 4);
  CHECK(sw.stored_lap_count() == 2);
  CHECK(sw.laps_dropped() == 2);
  // sum and average fold ALL laps, including dropped ones
  CHECK(std::chrono::duration_cast<ms>(sw.lap_sum()) == 100ms);
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_average()) == 25ms);  // 100/4
  // min/max only over the STORED (first two: 10, 20)
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_min()) == 10ms);
  CHECK(std::chrono::duration_cast<ms>(*sw.lap_max()) == 20ms);
}

TEST_CASE("nexenne::chrono::static_stopwatch overflowing lap still returns the segment") {
  using clk = ch::basic_manual_clock<struct fsw_ret_tag>;
  clk::reset();
  ch::static_stopwatch<1, clk> sw;
  sw.start();
  clk::advance(10ms);
  nexenne::utility::discard(sw.lap());  // fills the buffer
  clk::advance(33ms);
  auto const l{sw.lap()};  // dropped from storage but still returned
  REQUIRE(l.has_value());
  CHECK(std::chrono::duration_cast<ms>(*l) == 33ms);
  CHECK(sw.stored_lap_count() == 1);
  CHECK(sw.laps_dropped() == 1);
}

TEST_CASE("nexenne::chrono::static_stopwatch clear_laps zeroes all lap counters") {
  using clk = ch::basic_manual_clock<struct fsw_clear_tag>;
  clk::reset();
  ch::static_stopwatch<2, clk> sw;
  sw.start();
  for (int s : {10, 20, 30}) {
    clk::advance(ms{s});
    nexenne::utility::discard(sw.lap());
  }
  CHECK(sw.laps_dropped() == 1);
  sw.clear_laps();
  CHECK(sw.lap_count() == 0);
  CHECK(sw.stored_lap_count() == 0);
  CHECK(sw.laps_dropped() == 0);
  CHECK(sw.lap_sum() == clk::duration::zero());
  CHECK(sw.is_running());
  // rebase: next lap measures from now, total elapsed preserved
  clk::advance(5ms);
  auto const l{sw.lap()};
  REQUIRE(l.has_value());
  CHECK(std::chrono::duration_cast<ms>(*l) == 5ms);
  CHECK(std::chrono::duration_cast<ms>(sw.elapsed()) == 65ms);
}

TEST_CASE("nexenne::chrono::static_stopwatch reset and restart clear overflow state") {
  using clk = ch::basic_manual_clock<struct fsw_reset_tag>;
  clk::reset();
  ch::static_stopwatch<1, clk> sw;
  sw.start();
  for (int i{0}; i < 3; ++i) {
    clk::advance(10ms);
    nexenne::utility::discard(sw.lap());
  }
  CHECK(sw.laps_dropped() == 2);
  sw.reset();
  CHECK(sw.is_idle());
  CHECK(sw.lap_count() == 0);
  CHECK(sw.stored_lap_count() == 0);
  CHECK(sw.laps_dropped() == 0);
  CHECK(sw.elapsed() == clk::duration::zero());
  sw.restart();
  CHECK(sw.is_running());
  CHECK(sw.lap_count() == 0);
  CHECK(sw.laps_dropped() == 0);
}

TEST_CASE("nexenne::chrono::static_stopwatch comparison operators order by elapsed") {
  using clk = ch::basic_manual_clock<struct fsw_cmp_tag>;
  clk::reset();
  ch::static_stopwatch<2, clk> a;
  ch::static_stopwatch<2, clk> b;
  a.start();
  clk::advance(40ms);
  b.start();
  clk::advance(10ms);
  CHECK(a > b);
  CHECK(b < a);
  CHECK(a != b);
  CHECK_FALSE(a == b);
  CHECK((a <=> b) == std::strong_ordering::greater);
}

TEST_CASE("nexenne::chrono::static_stopwatch start/pause/resume no-op guards") {
  using clk = ch::basic_manual_clock<struct fsw_guard_tag>;
  clk::reset();
  ch::static_stopwatch<2, clk> sw;
  sw.pause();   // no-op idle
  sw.resume();  // no-op idle
  CHECK(sw.is_idle());
  sw.start();
  clk::advance(30ms);
  sw.start();  // no-op while running
  CHECK(sw.template elapsed<ms>() == 30ms);
  sw.pause();
  clk::advance(10ms);
  sw.pause();  // no-op while paused
  CHECK(sw.template elapsed<ms>() == 30ms);
  sw.resume();
  sw.resume();  // no-op while running
  clk::advance(5ms);
  CHECK(sw.template elapsed<ms>() == 35ms);
}

}  // namespace
