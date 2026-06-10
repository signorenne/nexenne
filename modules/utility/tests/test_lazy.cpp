/**
 * @file
 * @brief Tests for nexenne::utility::lazy.
 */

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/utility/lazy.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(
  !std::is_move_constructible_v<util::lazy<int (*)()>>, "lazy is non-movable (std::once_flag)"
);

TEST_CASE("nexenne::utility::lazy runs the factory once on first access") {
  int runs{0};
  auto value{util::lazy{[&] {
    ++runs;
    return 42;
  }}};

  CHECK_FALSE(value.has_value());
  CHECK(runs == 0);

  CHECK(*value == 42);
  CHECK(value.has_value());
  CHECK(runs == 1);

  CHECK(value.get() == 42);
  CHECK(runs == 1);  // cached, not re-run
}

TEST_CASE("nexenne::utility::lazy operator-> and const access materialise") {
  auto pair{util::lazy{[] { return std::pair{1, 2}; }}};
  CHECK(pair->first == 1);

  auto const& cref{pair};
  CHECK(cref->second == 2);
  CHECK(cref.has_value());
}

TEST_CASE("nexenne::utility::lazy runs the factory exactly once under contention") {
  constexpr std::size_t thread_count{16};
  std::atomic<int> runs{0};
  auto value{util::lazy{[&] {
    runs.fetch_add(1, std::memory_order_relaxed);
    return 7;
  }}};

  std::array<int, thread_count> results{};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (std::size_t i{0}; i < thread_count; ++i) {
    threads.emplace_back([&value, &results, i] { results[i] = *value; });
  }
  for (auto& t : threads) {
    t.join();
  }

  CHECK(runs.load() == 1);
  for (auto const r : results) {
    CHECK(r == 7);
  }
}

TEST_CASE("nexenne::utility::lazy retries after the factory throws") {
  int attempts{0};
  auto value{util::lazy{[&]() -> int {
    if (++attempts == 1) {
      throw std::runtime_error{"first attempt fails"};
    }
    return 99;
  }}};

  CHECK_THROWS_AS(static_cast<void>(*value), std::runtime_error);
  CHECK_FALSE(value.has_value());

  CHECK(*value == 99);  // call_once did not latch the throwing attempt
  CHECK(attempts == 2);
}

}  // namespace
