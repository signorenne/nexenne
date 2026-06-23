/**
 * @file
 * @brief Tests for the heap-free stream_logger (stack buffer, pluggable writer).
 */

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/stream_logger.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace lg = nexenne::logging;

// Opens a fresh temp file for writing; the caller closes it.
[[nodiscard]] auto open_temp(std::filesystem::path const& path) -> std::FILE* {
  std::filesystem::remove(path);
  return std::fopen(path.string().c_str(), "wb");
}

[[nodiscard]] auto read_all(std::filesystem::path const& path) -> std::string {
  auto in{std::ifstream{path}};
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

TEST_CASE("nexenne::logging::stream_logger writes a formatted line to a FILE*") {
  auto const path{std::filesystem::temp_directory_path() / "nexenne_stream_logger_basic.log"};
  auto* const f{open_temp(path)};
  REQUIRE(f != nullptr);
  {
    lg::stream_logger log{"dev", lg::level::trace, lg::file_writer{f}};
    log.info("value={}", 7);
  }
  nexenne::utility::discard(std::fclose(f));

  auto const s{read_all(path)};
  CHECK(s.find("[INFO ]") != std::string::npos);
  CHECK(s.find("[dev]") != std::string::npos);
  CHECK(s.find("-- value=7") != std::string::npos);
  CHECK(s.find("test_stream_logger.cpp:") != std::string::npos);  // file:line prefix
  CHECK(!s.empty());
  CHECK(s.back() == '\n');
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::stream_logger truncates an overlong message with an ellipsis") {
  auto const path{std::filesystem::temp_directory_path() / "nexenne_stream_logger_trunc.log"};
  auto* const f{open_temp(path)};
  REQUIRE(f != nullptr);
  {
    lg::basic_stream_logger<lg::file_writer, 64> log{"x", lg::level::trace, lg::file_writer{f}};
    log.info("{}", std::string(200, 'A'));
  }
  nexenne::utility::discard(std::fclose(f));

  auto const s{read_all(path)};
  CHECK(s.size() <= 64);                      // never exceeds the stack buffer
  CHECK(s.find("...") != std::string::npos);  // truncation marker
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::stream_logger respects the runtime level filter") {
  auto const path{std::filesystem::temp_directory_path() / "nexenne_stream_logger_filter.log"};
  auto* const f{open_temp(path)};
  REQUIRE(f != nullptr);
  {
    lg::stream_logger log{"x", lg::level::trace, lg::file_writer{f}};
    log.set_min_level(lg::level::warn);
    log.info("dropped");
    log.warn("kept");
  }
  nexenne::utility::discard(std::fclose(f));

  auto const s{read_all(path)};
  CHECK(s.find("dropped") == std::string::npos);
  CHECK(s.find("kept") != std::string::npos);
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::stream_logger with a null file_writer emits nothing and does not crash"
) {
  lg::stream_logger log{"x", lg::level::trace, lg::file_writer{nullptr}};
  log.info("nothing");
  log.error("still nothing");
  log.writer().stream = nullptr;  // retarget via the writer accessor
  log.warn("ignored");
  CHECK(true);
}

TEST_CASE("nexenne::logging::stream_logger drives a custom (non-FILE*) writer") {
  // The embedded use case: a writer with no FILE*, e.g. a UART or RTT channel.
  // Here it appends the formatted bytes to a test-owned string.
  struct buffer_writer {
    std::string* out;

    auto operator()(std::span<char const> const bytes) const noexcept -> void {
      out->append(bytes.data(), bytes.size());
    }
  };

  std::string captured;
  {
    lg::basic_stream_logger<buffer_writer> log{"sys", lg::level::trace, buffer_writer{&captured}};
    log.warn("temp={}C", 42);
    log.error("fault {}", 7);
  }

  CHECK(captured.find("[WARN ] [sys]") != std::string::npos);
  CHECK(captured.find("-- temp=42C") != std::string::npos);
  CHECK(captured.find("[ERROR] [sys]") != std::string::npos);
  CHECK(captured.find("-- fault 7") != std::string::npos);
  CHECK(captured.back() == '\n');
}

TEST_CASE("nexenne::logging::stream_logger exposes name, level, and enabled accessors") {
  lg::stream_logger log{"abc"};
  CHECK(log.name() == "abc");
  CHECK(log.buffer_size == 256);
  CHECK(log.enabled(lg::level::trace));
  log.set_min_level(lg::level::error);
  CHECK_FALSE(log.enabled(lg::level::warn));
  CHECK(log.enabled(lg::level::error));
  CHECK(log.enabled(lg::level::critical));
}

}  // namespace
