/**
 * @file
 * @brief Tests for nexenne::container::mpmc_queue.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/mpmc_queue.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::mpmc_queue single-threaded fill, drain, full, empty") {
  cn::mpmc_queue<int, 4> q;
  CHECK(q.capacity() == 4);
  CHECK(q.empty_approx());
  for (int i{0}; i < 4; ++i) {
    CHECK(q.push(i).has_value());
  }
  CHECK(q.full_approx());
  CHECK(q.push(99).error() == cn::container_error::full);
  for (int i{0}; i < 4; ++i) {
    auto v{q.pop()};
    REQUIRE(v.has_value());
    CHECK(*v == i);  // FIFO under single-threaded use
  }
  CHECK(q.pop().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::mpmc_queue emplace and try_pop") {
  cn::mpmc_queue<std::pair<int, int>, 4> q;
  CHECK(q.emplace(5, 6).has_value());
  auto v{q.try_pop()};
  REQUIRE(v.has_value());
  CHECK(v->first == 5);
  CHECK(v->second == 6);
  CHECK_FALSE(q.try_pop().has_value());
}

TEST_CASE("nexenne::container::mpmc_queue size_approx and max_size track occupancy") {
  cn::mpmc_queue<int, 8> q;
  CHECK(q.max_size() == 8);
  CHECK(q.capacity_value == 8);
  CHECK(q.size_approx() == 0);
  CHECK(q.empty_approx());
  CHECK_FALSE(q.full_approx());
  for (int i{0}; i < 4; ++i) {
    CHECK(q.push(i).has_value());
    CHECK(q.size_approx() == static_cast<std::size_t>(i + 1));
  }
  REQUIRE(q.pop().has_value());
  CHECK(q.size_approx() == 3);
}

TEST_CASE("nexenne::container::mpmc_queue wraps around the ring across many laps") {
  cn::mpmc_queue<int, 4> q;
  for (int round{0}; round < 20; ++round) {
    for (int i{0}; i < 4; ++i) {
      CHECK(q.push(round * 4 + i).has_value());
    }
    CHECK(q.push(-1).error() == cn::container_error::full);
    for (int i{0}; i < 4; ++i) {
      auto v{q.pop()};
      REQUIRE(v.has_value());
      CHECK(*v == round * 4 + i);  // FIFO within each lap
    }
    CHECK(q.empty_approx());
  }
}

TEST_CASE("nexenne::container::mpmc_queue holds non-trivial std::string elements") {
  cn::mpmc_queue<std::string, 4> q;
  CHECK(q.push(std::string(120, 'z')).has_value());
  CHECK(q.emplace(60, 'w').has_value());
  std::string const original{"stay"};
  CHECK(q.push(original).has_value());
  CHECK(original == "stay");
  CHECK(q.push("d").has_value());
  CHECK(q.push("overflow").error() == cn::container_error::full);

  auto a{q.pop()};
  REQUIRE(a.has_value());
  CHECK(*a == std::string(120, 'z'));
  auto b{q.pop()};
  REQUIRE(b.has_value());
  CHECK(*b == std::string(60, 'w'));
  auto c{q.pop()};
  REQUIRE(c.has_value());
  CHECK(*c == "stay");
}

TEST_CASE("nexenne::container::mpmc_queue holds a move-only type") {
  cn::mpmc_queue<std::unique_ptr<int>, 4> q;
  CHECK(q.push(std::make_unique<int>(9)).has_value());
  auto v{q.pop()};
  REQUIRE(v.has_value());
  CHECK(**v == 9);
}

TEST_CASE("nexenne::container::mpmc_queue moves from the source unique_ptr") {
  cn::mpmc_queue<std::unique_ptr<int>, 4> q;
  auto p{std::make_unique<int>(13)};
  CHECK(q.push(std::move(p)).has_value());
  CHECK(p == nullptr);
  auto v{q.pop()};
  REQUIRE(v.has_value());
  CHECK(**v == 13);
}

TEST_CASE("nexenne::container::mpmc_queue try_pop on empty returns nullopt") {
  cn::mpmc_queue<int, 4> q;
  CHECK_FALSE(q.try_pop().has_value());
}

TEST_CASE("nexenne::container::mpmc_queue destructor drains remaining move-only elements") {
  // Fill and leave queued; destructor must free each unique_ptr exactly once.
  cn::mpmc_queue<std::unique_ptr<int>, 8> q;
  for (int i{0}; i < 8; ++i) {
    CHECK(q.push(std::make_unique<int>(i)).has_value());
  }
  CHECK(q.full_approx());
  CHECK(q.push(std::make_unique<int>(99)).error() == cn::container_error::full);
}

TEST_CASE("nexenne::container::mpmc_queue many producers and consumers conserve every item") {
  constexpr int producers{4};
  constexpr int consumers{4};
  constexpr int per_producer{50000};
  constexpr int total{producers * per_producer};
  cn::mpmc_queue<int, 1024> q;

  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::atomic<std::int64_t> consumed_sum{0};

  {
    std::vector<std::jthread> threads;
    for (int p{0}; p < producers; ++p) {
      threads.emplace_back([&q, &produced] {
        for (int i{0}; i < per_producer; ++i) {
          while (!q.push(1).has_value()) {
            // full: spin until a consumer frees a slot
          }
          produced.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    for (int c{0}; c < consumers; ++c) {
      threads.emplace_back([&q, &consumed, &consumed_sum] {
        while (consumed.load(std::memory_order_relaxed) < total) {
          if (auto v{q.try_pop()}) {
            consumed_sum.fetch_add(*v, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }
  }  // join all

  CHECK(produced.load() == total);
  CHECK(consumed.load() == total);
  CHECK(consumed_sum.load() == total);  // every item carried value 1, none lost or duplicated
}

TEST_CASE("nexenne::container::mpmc_queue conserves distinct payloads across producers/consumers") {
  // Producer p pushes ids [p*per_producer, ...); every consumer marks ids it
  // pops. Each id must be marked exactly once across all consumers, catching any
  // loss or duplication a CAS race on either counter could introduce.
  constexpr int producers{4};
  constexpr int consumers{4};
  constexpr int per_producer{40000};
  constexpr int total{producers * per_producer};
  cn::mpmc_queue<int, 1024> q;
  std::vector<std::atomic<int>> seen(static_cast<std::size_t>(total));
  std::atomic<int> consumed{0};

  {
    std::vector<std::jthread> threads;
    for (int p{0}; p < producers; ++p) {
      threads.emplace_back([&q, p] {
        int const base{p * per_producer};
        for (int i{0}; i < per_producer; ++i) {
          while (!q.push(base + i).has_value()) {
            // spin
          }
        }
      });
    }
    for (int c{0}; c < consumers; ++c) {
      threads.emplace_back([&q, &seen, &consumed, total] {
        while (consumed.load(std::memory_order_relaxed) < total) {
          if (auto v{q.try_pop()}) {
            REQUIRE(*v >= 0);
            REQUIRE(*v < total);
            seen[static_cast<std::size_t>(*v)].fetch_add(1, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }
  }  // join

  CHECK(consumed.load() == total);
  bool each_once{true};
  for (auto const& c : seen) {
    if (c.load(std::memory_order_relaxed) != 1) {
      each_once = false;
      break;
    }
  }
  CHECK(each_once);
}

TEST_CASE("nexenne::container::mpmc_queue conserves move-only elements across producers/consumers"
) {
  constexpr int producers{4};
  constexpr int consumers{4};
  constexpr int per_producer{15000};
  constexpr int total{producers * per_producer};
  cn::mpmc_queue<std::unique_ptr<int>, 512> q;
  std::vector<std::atomic<int>> seen(static_cast<std::size_t>(total));
  std::atomic<int> consumed{0};

  {
    std::vector<std::jthread> threads;
    for (int p{0}; p < producers; ++p) {
      threads.emplace_back([&q, p] {
        int const base{p * per_producer};
        for (int i{0}; i < per_producer; ++i) {
          while (!q.push(std::make_unique<int>(base + i)).has_value()) {
            // spin
          }
        }
      });
    }
    for (int c{0}; c < consumers; ++c) {
      threads.emplace_back([&q, &seen, &consumed, total] {
        while (consumed.load(std::memory_order_relaxed) < total) {
          if (auto v{q.try_pop()}) {
            REQUIRE(*v != nullptr);
            seen[static_cast<std::size_t>(**v)].fetch_add(1, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }
  }  // join

  CHECK(consumed.load() == total);
  bool each_once{true};
  for (auto const& c : seen) {
    if (c.load(std::memory_order_relaxed) != 1) {
      each_once = false;
      break;
    }
  }
  CHECK(each_once);  // no leak/double-free under LSan; exact conservation
}

}  // namespace
