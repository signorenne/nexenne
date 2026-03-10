/**
 * @file
 * @brief Tests for the multi_sink fan-out composite sink.
 */

#include <doctest/doctest.h>

#include <memory>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/multi_sink.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/sink.hpp>

namespace {

namespace lg = nexenne::logging;

// A test sink that records every line that passes the base-class level filter.
// Shares a vector with the test so captures remain observable after the sink is
// adopted by a multi_sink.
class capture_sink final : public lg::sink {
public:
  explicit capture_sink(std::vector<std::string>* const out) noexcept : m_out{out} {}

protected:
  auto write_out(lg::record const& r) noexcept -> void override {
    m_out->push_back(default_format(r));
  }

  auto flush_out() noexcept -> void override {
    flushes += 1;
  }

public:
  std::size_t flushes{0};  ///< Count of flush_out invocations on this sink.

private:
  std::vector<std::string>* m_out;
};

[[nodiscard]] auto make_record(lg::level const sev, std::string msg) -> lg::record {
  return lg::record{sev, std::source_location::current(), "net", std::move(msg)};
}

TEST_CASE("nexenne::logging::multi_sink fans one record out to every child") {
  std::vector<std::string> a;
  std::vector<std::string> b;
  lg::multi_sink ms;
  ms.add(std::make_unique<capture_sink>(&a));
  ms.add(std::make_unique<capture_sink>(&b));
  CHECK(ms.child_count() == 2);

  ms.write(make_record(lg::level::info, "hi"));
  REQUIRE(a.size() == 1);
  REQUIRE(b.size() == 1);
  CHECK(a[0].find("-- hi") != std::string::npos);
  CHECK(b[0].find("-- hi") != std::string::npos);
}

TEST_CASE("nexenne::logging::multi_sink applies each child's own level filter") {
  std::vector<std::string> low;
  std::vector<std::string> high;
  auto low_sink{std::make_unique<capture_sink>(&low)};
  auto high_sink{std::make_unique<capture_sink>(&high)};
  high_sink->set_min_level(lg::level::error);  // only error-and-up reaches high
  lg::multi_sink ms;
  ms.add(std::move(low_sink));
  ms.add(std::move(high_sink));

  ms.write(make_record(lg::level::info, "i"));   // low only
  ms.write(make_record(lg::level::error, "e"));  // both

  REQUIRE(low.size() == 2);
  REQUIRE(high.size() == 1);
  CHECK(high[0].find("-- e") != std::string::npos);
}

TEST_CASE("nexenne::logging::multi_sink with no children is a write/flush no-op") {
  lg::multi_sink ms;
  CHECK(ms.child_count() == 0);
  ms.write(make_record(lg::level::critical, "x"));  // must not crash
  ms.flush();                                       // must not crash
  CHECK(ms.child_count() == 0);
}

TEST_CASE("nexenne::logging::multi_sink fans flush out to every child") {
  std::vector<std::string> a;
  std::vector<std::string> b;
  auto sa{std::make_unique<capture_sink>(&a)};
  auto sb{std::make_unique<capture_sink>(&b)};
  auto* const sa_raw{sa.get()};
  auto* const sb_raw{sb.get()};
  lg::multi_sink ms;
  ms.add(std::move(sa));
  ms.add(std::move(sb));

  ms.flush();
  ms.flush();
  CHECK(sa_raw->flushes == 2);
  CHECK(sb_raw->flushes == 2);
}

TEST_CASE("nexenne::logging::multi_sink dispatches in insertion order") {
  std::vector<std::string> order;

  // Both children append to the same vector with a distinct prefix so arrival
  // order is observable.
  class tagged_sink final : public lg::sink {
  public:
    tagged_sink(std::vector<std::string>* const out, std::string tag) noexcept
        : m_out{out}, m_tag{std::move(tag)} {}

  protected:
    auto write_out(lg::record const&) noexcept -> void override {
      m_out->push_back(m_tag);
    }

    auto flush_out() noexcept -> void override {}

  private:
    std::vector<std::string>* m_out;
    std::string m_tag;
  };

  lg::multi_sink ms;
  ms.add(std::make_unique<tagged_sink>(&order, "first"));
  ms.add(std::make_unique<tagged_sink>(&order, "second"));
  ms.write(make_record(lg::level::info, "m"));

  REQUIRE(order.size() == 2);
  CHECK(order[0] == "first");
  CHECK(order[1] == "second");
}

TEST_CASE("nexenne::logging::multi_sink ignores a null child") {
  lg::multi_sink ms;
  ms.add(nullptr);
  CHECK(ms.child_count() == 0);
}

TEST_CASE("nexenne::logging::multi_sink's own level filter gates before fan-out") {
  std::vector<std::string> a;
  lg::multi_sink ms;
  ms.add(std::make_unique<capture_sink>(&a));
  ms.set_min_level(lg::level::warn);

  ms.write(make_record(lg::level::info, "dropped"));  // gated by the composite
  ms.write(make_record(lg::level::warn, "kept"));
  REQUIRE(a.size() == 1);
  CHECK(a[0].find("-- kept") != std::string::npos);
}

}  // namespace
