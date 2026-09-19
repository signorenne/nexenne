/**
 * @file
 * @brief Tests for the size-based rotating file sink.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/rotating_file_sink.hpp>

namespace {

namespace lg = nexenne::logging;

[[nodiscard]] auto make_record(lg::level const sev, std::string msg) -> lg::record {
  return lg::record{sev, std::source_location::current(), "net", std::move(msg)};
}

[[nodiscard]] auto read_file(std::filesystem::path const& p) -> std::string {
  auto in{std::ifstream{p}};
  if (!in.is_open()) {
    return {};
  }
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

[[nodiscard]] auto fresh_base(std::string_view const name) -> std::filesystem::path {
  auto const base{std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / name};
  for (std::size_t i{0}; i <= 8; i = i + 1) {
    std::filesystem::remove(
      i == 0 ? base : std::filesystem::path{base.string() + "." + std::to_string(i)}
    );
  }
  return base;
}

auto cleanup(std::filesystem::path const& base) -> void {
  for (std::size_t i{0}; i <= 8; i = i + 1) {
    std::filesystem::remove(
      i == 0 ? base : std::filesystem::path{base.string() + "." + std::to_string(i)}
    );
  }
}

TEST_CASE("nexenne::logging::rotating_file_sink reports its rotation limits") {
  lg::rotating_file_sink const s{std::string{__FILE__} + "/unopenable.log", 4096, 3};
  CHECK_FALSE(s.is_open());
  CHECK(s.max_bytes() == 4096);
  CHECK(s.max_files() == 3);
}

TEST_CASE("nexenne::logging::rotating_file_sink writes below the limit stay in one file") {
  auto const base{fresh_base("nexenne_rfs_below.log")};
  {
    lg::rotating_file_sink s{base.string(), 100'000, 3};
    REQUIRE(s.is_open());
    s.write(make_record(lg::level::info, "one"));
    s.write(make_record(lg::level::info, "two"));
    s.flush();
    CHECK(s.current_size() > 0);
  }
  auto const contents{read_file(base)};
  CHECK(contents.find("-- one") != std::string::npos);
  CHECK(contents.find("-- two") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(base.string() + ".1"));
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink rotates once a write would cross the limit") {
  auto const base{fresh_base("nexenne_rfs_cross.log")};
  {
    lg::rotating_file_sink s{base.string(), 50, 3};
    REQUIRE(s.is_open());
    s.write(make_record(lg::level::info, "first"));
    auto const after_first{s.current_size()};
    CHECK(after_first > 0);
    s.write(make_record(lg::level::info, "second"));
    CHECK(s.current_size() < after_first + after_first);
    s.flush();
  }
  auto const active{read_file(base)};
  auto const backup{read_file(base.string() + ".1")};
  CHECK(backup.find("-- first") != std::string::npos);
  CHECK(active.find("-- second") != std::string::npos);
  CHECK(active.find("-- first") == std::string::npos);
  cleanup(base);
}

TEST_CASE(
  "nexenne::logging::rotating_file_sink keeps the backup count bounded, dropping the oldest"
) {
  auto const base{fresh_base("nexenne_rfs_bound.log")};
  {
    lg::rotating_file_sink s{base.string(), 50, 2};
    REQUIRE(s.is_open());
    s.write(make_record(lg::level::info, "aaa"));
    s.write(make_record(lg::level::info, "bbb"));
    s.write(make_record(lg::level::info, "ccc"));
    s.write(make_record(lg::level::info, "ddd"));
    s.flush();
  }
  CHECK(std::filesystem::exists(base));
  CHECK(std::filesystem::exists(base.string() + ".1"));
  CHECK(std::filesystem::exists(base.string() + ".2"));
  CHECK_FALSE(std::filesystem::exists(base.string() + ".3"));

  CHECK(read_file(base).find("-- ddd") != std::string::npos);
  CHECK(read_file(base.string() + ".1").find("-- ccc") != std::string::npos);
  CHECK(read_file(base.string() + ".2").find("-- bbb") != std::string::npos);
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink rotated files hold the expected lines in order") {
  auto const base{fresh_base("nexenne_rfs_order.log")};
  {
    lg::rotating_file_sink s{base.string(), 50, 5};
    REQUIRE(s.is_open());
    for (auto const* const msg : {"r0", "r1", "r2"}) {
      s.write(make_record(lg::level::info, msg));
    }
    s.flush();
  }
  CHECK(read_file(base).find("-- r2") != std::string::npos);
  CHECK(read_file(base.string() + ".1").find("-- r1") != std::string::npos);
  CHECK(read_file(base.string() + ".2").find("-- r0") != std::string::npos);
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink force_rotate archives without crossing the limit") {
  auto const base{fresh_base("nexenne_rfs_force.log")};
  {
    lg::rotating_file_sink s{base.string(), 100'000, 3};
    REQUIRE(s.is_open());
    s.write(make_record(lg::level::info, "before"));
    s.force_rotate();
    CHECK(s.current_size() == 0);
    s.write(make_record(lg::level::info, "after"));
    s.flush();
  }
  CHECK(read_file(base.string() + ".1").find("-- before") != std::string::npos);
  CHECK(read_file(base).find("-- after") != std::string::npos);
  cleanup(base);
}

TEST_CASE(
  "nexenne::logging::rotating_file_sink with max_files == 0 truncates instead of archiving"
) {
  auto const base{fresh_base("nexenne_rfs_trunc.log")};
  {
    lg::rotating_file_sink s{base.string(), 50, 0};
    REQUIRE(s.is_open());
    s.write(make_record(lg::level::info, "gone"));
    s.write(make_record(lg::level::info, "kept"));
    s.flush();
  }
  CHECK_FALSE(std::filesystem::exists(base.string() + ".1"));
  auto const active{read_file(base)};
  CHECK(active.find("-- kept") != std::string::npos);
  CHECK(active.find("-- gone") == std::string::npos);
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink force_rotate is safe against concurrent writes") {
  auto const base{fresh_base("nexenne_rfs_race.log")};
  {
    lg::rotating_file_sink s{base.string(), 512, 4};
    REQUIRE(s.is_open());
    std::atomic<bool> stop{false};
    auto writer{std::thread{[&s, &stop] {
      for (std::size_t i{0}; !stop.load(std::memory_order_acquire); ++i) {
        s.write(make_record(lg::level::info, std::to_string(i)));
      }
    }}};
    for (std::size_t r{0}; r < 50; ++r) {
      s.force_rotate();
    }
    stop.store(true, std::memory_order_release);
    writer.join();
    CHECK(s.is_open());
  }
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink reports a failed open of a directory path") {
  lg::rotating_file_sink bad{std::string_view{NEXENNE_LOGGING_TEST_DIR}, 1'000, 3};
  CHECK_FALSE(bad.is_open());
}

TEST_CASE("nexenne::logging::rotating_file_sink seeds its size from a pre-existing file") {
  auto const base{fresh_base("nexenne_rfs_resume.log")};
  {
    lg::rotating_file_sink s{base.string(), 100'000, 3};
    s.write(make_record(lg::level::info, "seed"));
    s.flush();
  }
  auto const grown{std::filesystem::file_size(base)};
  {
    lg::rotating_file_sink s{base.string(), 100'000, 3};
    CHECK(s.current_size() == grown);
  }
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink reopens after a failed rotation") {
  auto const dir{std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / "nexenne_rfs_reopen"};
  std::filesystem::remove_all(dir);
  std::filesystem::create_directory(dir);
  auto const base{dir / "app.log"};
  lg::rotating_file_sink s{base.string(), 100'000, 2};
  REQUIRE(s.is_open());

  std::filesystem::remove_all(dir);
  s.force_rotate();
  CHECK_FALSE(s.is_open());

  std::filesystem::create_directory(dir);
  s.write(make_record(lg::level::info, "back"));
  s.flush();
  CHECK(s.is_open());
  CHECK(read_file(base).find("-- back") != std::string::npos);
  std::filesystem::remove_all(dir);
}

}  // namespace
