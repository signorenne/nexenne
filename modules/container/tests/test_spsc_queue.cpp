/**
 * @file
 * @brief Tests for nexenne::container::spsc_queue.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/spsc_queue.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::spsc_queue fills to capacity then reports full") {
  cn::spsc_queue<int, 4> q;  // capacity 3
  CHECK(q.capacity() == 3);
  CHECK(q.empty_approx());
  CHECK(q.push(1).has_value());
  CHECK(q.push(2).has_value());
  CHECK(q.push(3).has_value());
  CHECK(q.full_approx());
  CHECK(q.push(4).error() == cn::container_error::full);
  CHECK(q.size_approx() == 3);
}

TEST_CASE("nexenne::container::spsc_queue pops in FIFO order") {
  cn::spsc_queue<int, 8> q;
  for (int i{0}; i < 5; ++i) {
    CHECK(q.push(i).has_value());
  }
  for (int i{0}; i < 5; ++i) {
    auto v{q.pop()};
    REQUIRE(v.has_value());
    CHECK(*v == i);
  }
  CHECK(q.pop().error() == cn::container_error::empty);
  CHECK(q.empty_approx());
}

TEST_CASE("nexenne::container::spsc_queue wraps around the ring") {
  cn::spsc_queue<int, 4> q;  // capacity 3
  for (int round{0}; round < 10; ++round) {
    CHECK(q.push(round).has_value());
    auto v{q.try_pop()};
    REQUIRE(v.has_value());
    CHECK(*v == round);  // push/pop one at a time wraps cleanly
  }
}

TEST_CASE("nexenne::container::spsc_queue emplace and try_pop") {
  cn::spsc_queue<std::pair<int, int>, 4> q;
  CHECK(q.emplace(1, 2).has_value());
  auto v{q.try_pop()};
  REQUIRE(v.has_value());
  CHECK(v->first == 1);
  CHECK(v->second == 2);
  CHECK_FALSE(q.try_pop().has_value());
}

TEST_CASE("nexenne::container::spsc_queue non-power-of-two N wraps via modulo") {
  cn::spsc_queue<int, 3> q;  // capacity 2, exercises the modulo branch of next()
  CHECK(q.capacity() == 2);
  for (int round{0}; round < 12; ++round) {
    CHECK(q.push(round).has_value());
    CHECK(q.push(round + 100).has_value());
    CHECK(q.push(round + 200).error() == cn::container_error::full);  // capacity is 2
    auto a{q.pop()};
    auto b{q.pop()};
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == round);
    CHECK(*b == round + 100);
    CHECK(q.empty_approx());
  }
}

TEST_CASE("nexenne::container::spsc_queue size_approx tracks fill and drain") {
  cn::spsc_queue<int, 8> q;  // capacity 7
  CHECK(q.size_approx() == 0);
  for (int i{0}; i < 5; ++i) {
    CHECK(q.push(i).has_value());
    CHECK(q.size_approx() == static_cast<std::size_t>(i + 1));
  }
  for (int i{0}; i < 5; ++i) {
    CHECK(q.size_approx() == static_cast<std::size_t>(5 - i));
    REQUIRE(q.pop().has_value());
  }
  CHECK(q.size_approx() == 0);
  CHECK(q.empty_approx());
  CHECK_FALSE(q.full_approx());
}

TEST_CASE("nexenne::container::spsc_queue max_size equals capacity") {
  CHECK(cn::spsc_queue<int, 16>::max_size() == 15);
  CHECK(cn::spsc_queue<int, 16>::capacity() == 15);
  CHECK(cn::spsc_queue<int, 16>::capacity_value == 15);
}

TEST_CASE("nexenne::container::spsc_queue try_pop on empty returns nullopt") {
  cn::spsc_queue<int, 4> q;
  CHECK_FALSE(q.try_pop().has_value());
  CHECK(q.empty_approx());
}

TEST_CASE("nexenne::container::spsc_queue holds non-trivial std::string elements") {
  cn::spsc_queue<std::string, 4> q;                  // capacity 3
  CHECK(q.push(std::string(100, 'x')).has_value());  // heap-allocated string
  CHECK(q.emplace(50, 'y').has_value());
  std::string const moved_in{"moved"};
  CHECK(q.push(moved_in).has_value());  // copy push, original untouched
  CHECK(moved_in == "moved");
  CHECK(q.push("overflow").error() == cn::container_error::full);

  auto a{q.pop()};
  REQUIRE(a.has_value());
  CHECK(*a == std::string(100, 'x'));
  auto b{q.pop()};
  REQUIRE(b.has_value());
  CHECK(*b == std::string(50, 'y'));
  auto c{q.pop()};
  REQUIRE(c.has_value());
  CHECK(*c == "moved");
}

TEST_CASE("nexenne::container::spsc_queue holds a move-only type") {
  cn::spsc_queue<std::unique_ptr<int>, 4> q;
  CHECK(q.push(std::make_unique<int>(7)).has_value());
  auto v{q.pop()};
  REQUIRE(v.has_value());
  CHECK(**v == 7);
}

TEST_CASE("nexenne::container::spsc_queue moves from the source unique_ptr") {
  cn::spsc_queue<std::unique_ptr<int>, 4> q;
  auto p{std::make_unique<int>(42)};
  CHECK(q.push(std::move(p)).has_value());
  CHECK(p == nullptr);  // ownership transferred into the queue
  auto v{q.pop()};
  REQUIRE(v.has_value());
  REQUIRE(*v != nullptr);
  CHECK(**v == 42);
}

TEST_CASE("nexenne::container::spsc_queue destructor drains remaining move-only elements") {
  // Fill with unique_ptr and never pop; the destructor must free every slot
  // (verified under ASan/LSan: no leak, no double free).
  cn::spsc_queue<std::unique_ptr<int>, 8> q;
  for (int i{0}; i < 7; ++i) {
    CHECK(q.push(std::make_unique<int>(i)).has_value());
  }
  CHECK(q.full_approx());
  // queue goes out of scope here with 7 live elements
}

TEST_CASE("nexenne::container::spsc_queue one producer and one consumer move every item") {
  constexpr int total{200000};
  cn::spsc_queue<int, 1024> q;
  std::int64_t consumed_sum{0};
  int consumed_count{0};

  {
    std::jthread producer{[&q] {
      for (int i{0}; i < total; ++i) {
        while (!q.push(i).has_value()) {
          // queue full: spin until the consumer drains a slot
        }
      }
    }};
    std::jthread consumer{[&q, &consumed_sum, &consumed_count] {
      while (consumed_count < total) {
        if (auto v{q.try_pop()}) {
          consumed_sum += *v;
          ++consumed_count;
        }
      }
    }};
  }  // jthreads join here

  CHECK(consumed_count == total);
  // sum of 0..total-1 must be conserved exactly (no lost or duplicated items)
  auto const expected{static_cast<std::int64_t>(total) * (total - 1) / 2};
  CHECK(consumed_sum == expected);
}

TEST_CASE("nexenne::container::spsc_queue preserves strict FIFO order under threading") {
  // The consumer records the exact dequeue sequence; for one producer/one
  // consumer the queue must hand back items in the exact order pushed.
  constexpr int total{100000};
  cn::spsc_queue<int, 256> q;
  std::vector<int> received;
  received.reserve(total);

  {
    std::jthread producer{[&q] {
      for (int i{0}; i < total; ++i) {
        while (!q.push(i).has_value()) {
          // spin until a slot frees
        }
      }
    }};
    std::jthread consumer{[&q, &received] {
      while (static_cast<int>(received.size()) < total) {
        if (auto v{q.try_pop()}) {
          received.push_back(*v);
        }
      }
    }};
  }  // join

  REQUIRE(static_cast<int>(received.size()) == total);
  bool ordered{true};
  for (int i{0}; i < total; ++i) {
    if (received[static_cast<std::size_t>(i)] != i) {
      ordered = false;
      break;
    }
  }
  CHECK(ordered);  // strict FIFO: item i dequeued at position i
}

TEST_CASE("nexenne::container::spsc_queue conserves move-only elements across threads") {
  // Each unique_ptr carries a distinct id; the consumer marks it seen exactly
  // once. A lost, duplicated, or leaked element shows up in the tally or LSan.
  constexpr int total{50000};
  cn::spsc_queue<std::unique_ptr<int>, 512> q;
  std::vector<int> seen(static_cast<std::size_t>(total), 0);

  {
    std::jthread producer{[&q] {
      for (int i{0}; i < total; ++i) {
        while (!q.push(std::make_unique<int>(i)).has_value()) {
          // spin
        }
      }
    }};
    std::jthread consumer{[&q, &seen] {
      int count{0};
      while (count < total) {
        if (auto v{q.try_pop()}) {
          REQUIRE(*v != nullptr);
          ++seen[static_cast<std::size_t>(**v)];
          ++count;
        }
      }
    }};
  }  // join

  bool each_once{true};
  for (auto const c : seen) {
    if (c != 1) {
      each_once = false;
      break;
    }
  }
  CHECK(each_once);  // every id consumed exactly once
}

}  // namespace
