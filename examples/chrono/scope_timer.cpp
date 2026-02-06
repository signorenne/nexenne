/**
 * @file
 * @brief scope_timer RAII measurement over a manual clock.
 *
 * A scope_timer captures the clock at construction and fires its callback with
 * the elapsed duration when it leaves scope. The manual clock is advanced inside
 * the scope so the measured value is deterministic.
 */

#include <chrono>
#include <print>

#include <nexenne/chrono/manual_clock.hpp>
#include <nexenne/chrono/scope_timer.hpp>

namespace {

namespace ch = nexenne::chrono;
using clk = ch::basic_manual_clock<struct st_example_tag>;

}  // namespace

auto main() -> int {
  using namespace std::chrono_literals;

  clk::reset();
  std::int64_t measured_us{0};
  auto record{[&measured_us](clk::duration d) {
    measured_us = std::chrono::duration_cast<std::chrono::microseconds>(d).count();
  }};

  {
    ch::scope_timer<decltype(record), clk> timer{record};
    clk::advance(250us);  // pretend the scope did 250 us of work
  }  // callback fires here

  std::println("scope took {} us", measured_us);
  // scope took 250 us
  return 0;
}
