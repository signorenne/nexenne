/**
 * @file
 * @brief Tests for the size-based rotating file sink.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <string>
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

// Reads a whole file into a string, or "" when it does not exist.
[[nodiscard]] auto read_file(std::filesystem::path const& p) -> std::string {
  auto in{std::ifstream{p}};
  if (!in.is_open()) {
    return {};
  }
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

// A unique base path under the temp directory, with its rotated siblings removed.
[[nodiscard]] auto fresh_base(std::string_view const name) -> std::filesystem::path {
  auto const base{std::filesystem::temp_directory_path() / name};
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
  // No rotation happened, so no backup exists.
  CHECK_FALSE(std::filesystem::exists(base.string() + ".1"));
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink rotates once a write would cross the limit") {
  auto const base{fresh_base("nexenne_rfs_cross.log")};
  // One formatted line is well over 50 bytes; a 50-byte cap forces rotation on
  // the second write.
  {
    lg::rotating_file_sink s{base.string(), 50, 3};
    REQUIRE(s.is_open());
    s.write(make_record(lg::level::info, "first"));
    auto const after_first{s.current_size()};
    CHECK(after_first > 0);
    s.write(make_record(lg::level::info, "second"));
    // After rotation the active file holds only the second record.
    CHECK(s.current_size() < after_first + after_first);
    s.flush();
  }
  auto const active{read_file(base)};
  auto const backup{read_file(base.string() + ".1")};
  CHECK(backup.find("-- first") != std::string::npos);   // rotated out
  CHECK(active.find("-- second") != std::string::npos);  // new active file
  CHECK(active.find("-- first") == std::string::npos);   // first is not duplicated
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink keeps the backup count bounded, dropping the oldest"
) {
  auto const base{fresh_base("nexenne_rfs_bound.log")};
  {
    // max_files = 2: at most foo.log.1 and foo.log.2 may exist.
    lg::rotating_file_sink s{base.string(), 50, 2};
    REQUIRE(s.is_open());
    // Four records each force a rotation: a, b, c, d.
    s.write(make_record(lg::level::info, "aaa"));
    s.write(make_record(lg::level::info, "bbb"));
    s.write(make_record(lg::level::info, "ccc"));
    s.write(make_record(lg::level::info, "ddd"));
    s.flush();
  }
  // The oldest two backups must be gone; only the newest two are retained.
  CHECK(std::filesystem::exists(base));                  // active holds ddd
  CHECK(std::filesystem::exists(base.string() + ".1"));  // ccc
  CHECK(std::filesystem::exists(base.string() + ".2"));  // bbb
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
  // After three single-record rotations: active=r2, .1=r1, .2=r0.
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
    CHECK(s.current_size() == 0);  // fresh active file
    s.write(make_record(lg::level::info, "after"));
    s.flush();
  }
  CHECK(read_file(base.string() + ".1").find("-- before") != std::string::npos);
  CHECK(read_file(base).find("-- after") != std::string::npos);
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink with max_files == 0 truncates instead of archiving"
) {
  auto const base{fresh_base("nexenne_rfs_trunc.log")};
  {
    lg::rotating_file_sink s{base.string(), 50, 0};
    REQUIRE(s.is_open());
    s.write(make_record(lg::level::info, "gone"));
    s.write(make_record(lg::level::info, "kept"));
    s.flush();
  }
  CHECK_FALSE(std::filesystem::exists(base.string() + ".1"));  // nothing archived
  auto const active{read_file(base)};
  CHECK(active.find("-- kept") != std::string::npos);
  CHECK(active.find("-- gone") == std::string::npos);  // previous content dropped
  cleanup(base);
}

TEST_CASE("nexenne::logging::rotating_file_sink reports a failed open") {
  // A directory cannot be opened for append.
  lg::rotating_file_sink bad{std::filesystem::temp_directory_path().string(), 1'000, 3};
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
    // Re-opening must pick up the existing bytes, not start from zero.
    lg::rotating_file_sink s{base.string(), 100'000, 3};
    CHECK(s.current_size() == grown);
  }
  cleanup(base);
}

}  // namespace
