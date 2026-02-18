/**
 * @file
 * @brief Tests for nexenne::container::mpsc_queue.
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
#include <nexenne/container/mpsc_queue.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::mpsc_queue single-threaded fill, drain, full, empty") {
  cn::mpsc_queue<int, 4> q;  // capacity 4 (Vyukov uses all slots)
  CHECK(q.capacity() == 4);
  CHECK(q.empty_approx());
  for (int i{0}; i < 4; ++i) {
    CHECK(q.push(i).has_value());
  }
  CHECK(q.push(99).error() == cn::container_error::full);
  for (int i{0}; i < 4; ++i) {
    auto v{q.pop()};
    REQUIRE(v.has_value());
    CHECK(*v == i);  // FIFO
  }
  CHECK(q.pop().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::mpsc_queue emplace and try_pop") {
  cn::mpsc_queue<std::pair<int, int>, 4> q;
  CHECK(q.emplace(3, 4).has_value());
  auto v{q.try_pop()};
  REQUIRE(v.has_value());
  CHECK(v->first == 3);
  CHECK(v->second == 4);
  CHECK_FALSE(q.try_pop().has_value());
}

TEST_CASE("nexenne::container::mpsc_queue size_approx and max_size") {
  cn::mpsc_queue<int, 8> q;
  CHECK(q.max_size() == 8);
  CHECK(q.capacity_value == 8);
  CHECK(q.size_approx() == 0);
  for (int i{0}; i < 3; ++i) {
    CHECK(q.push(i).has_value());
    CHECK(q.size_approx() == static_cast<std::size_t>(i + 1));
  }
  REQUIRE(q.pop().has_value());
  CHECK(q.size_approx() == 2);
  CHECK_FALSE(q.empty_approx());
}

TEST_CASE("nexenne::container::mpsc_queue holds non-trivial std::string elements") {
  cn::mpsc_queue<std::string, 4> q;
  CHECK(q.push(std::string(80, 'a')).has_value());
  CHECK(q.emplace(40, 'b').has_value());
  std::string const original{"keep"};
  CHECK(q.push(original).has_value());
  CHECK(original == "keep");  // copy push leaves source intact
  CHECK(q.push("d").has_value());
  CHECK(q.push("overflow").error() == cn::container_error::full);

  auto a{q.pop()};
  REQUIRE(a.has_value());
  CHECK(*a == std::string(80, 'a'));
  auto b{q.pop()};
  REQUIRE(b.has_value());
  CHECK(*b == std::string(40, 'b'));
  auto c{q.pop()};
  REQUIRE(c.has_value());
  CHECK(*c == "keep");
}

TEST_CASE("nexenne::container::mpsc_queue holds a move-only type") {
  cn::mpsc_queue<std::unique_ptr<int>, 4> q;
  CHECK(q.push(std::make_unique<int>(5)).has_value());
  auto v{q.pop()};
  REQUIRE(v.has_value());
  CHECK(**v == 5);
}

TEST_CASE("nexenne::container::mpsc_queue moves from the source unique_ptr") {
  cn::mpsc_queue<std::unique_ptr<int>, 4> q;
  auto p{std::make_unique<int>(11)};
  CHECK(q.push(std::move(p)).has_value());
  CHECK(p == nullptr);
  auto v{q.pop()};
  REQUIRE(v.has_value());
  CHECK(**v == 11);
}

TEST_CASE("nexenne::container::mpsc_queue try_pop on empty returns nullopt") {
  cn::mpsc_queue<int, 4> q;
  CHECK_FALSE(q.try_pop().has_value());
}

TEST_CASE("nexenne::container::mpsc_queue destructor drains remaining move-only elements") {
  // Fill and never drain; the destructor must free every queued unique_ptr
  // (LSan would flag any leak, ASan any double free).
  cn::mpsc_queue<std::unique_ptr<int>, 8> q;
  for (int i{0}; i < 8; ++i) {
    CHECK(q.push(std::make_unique<int>(i)).has_value());
  }
  CHECK(q.push(std::make_unique<int>(99)).error() == cn::container_error::full);
}

TEST_CASE("nexenne::container::mpsc_queue many producers and one consumer conserve every item") {
  constexpr int producers{4};
  constexpr int per_producer{50000};
  constexpr int total{producers * per_producer};
  cn::mpsc_queue<int, 1024> q;
  std::int64_t consumed_sum{0};
  int consumed_count{0};

  {
    std::vector<std::jthread> threads;
    // Each producer pushes value 1 (so the total sum equals the item count),
    // which makes a lost or duplicated item show up as a count/sum mismatch.
    for (int p{0}; p < producers; ++p) {
      threads.emplace_back([&q] {
        for (int i{0}; i < per_producer; ++i) {
          while (!q.push(1).has_value()) {
            // queue full: spin until the consumer drains a slot
          }
        }
      });
    }
    std::jthread consumer{[&q, &consumed_sum, &consumed_count] {
      while (consumed_count < total) {
        if (auto v{q.try_pop()}) {
          consumed_sum += *v;
          ++consumed_count;
        }
      }
    }};
  }  // join all

  CHECK(consumed_count == total);
  CHECK(consumed_sum == total);  // each item carried value 1
}

TEST_CASE("nexenne::container::mpsc_queue conserves distinct payloads from many producers") {
  // Producer p pushes ids [p*per_producer, (p+1)*per_producer); the consumer
  // marks each id seen. Every id must arrive exactly once: no loss, duplicate,
  // or corruption (which CAS-reservation races could otherwise cause).
  constexpr int producers{4};
  constexpr int per_producer{40000};
  constexpr int total{producers * per_producer};
  cn::mpsc_queue<int, 1024> q;
  std::vector<std::atomic<int>> seen(static_cast<std::size_t>(total));

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
    std::jthread consumer{[&q, &seen, total] {
      int count{0};
      while (count < total) {
        if (auto v{q.try_pop()}) {
          REQUIRE(*v >= 0);
          REQUIRE(*v < total);
          seen[static_cast<std::size_t>(*v)].fetch_add(1, std::memory_order_relaxed);
          ++count;
        }
      }
    }};
  }  // join

  bool each_once{true};
  for (auto const& c : seen) {
    if (c.load(std::memory_order_relaxed) != 1) {
      each_once = false;
      break;
    }
  }
  CHECK(each_once);
}

TEST_CASE("nexenne::container::mpsc_queue conserves move-only elements from many producers") {
  constexpr int producers{4};
  constexpr int per_producer{20000};
  constexpr int total{producers * per_producer};
  cn::mpsc_queue<std::unique_ptr<int>, 512> q;
  std::vector<std::atomic<int>> seen(static_cast<std::size_t>(total));

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
    std::jthread consumer{[&q, &seen, total] {
      int count{0};
      while (count < total) {
        if (auto v{q.try_pop()}) {
          REQUIRE(*v != nullptr);
          seen[static_cast<std::size_t>(**v)].fetch_add(1, std::memory_order_relaxed);
          ++count;
        }
      }
    }};
  }  // join

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
