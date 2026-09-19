/**
 * @file
 * @brief Tests for the heap-free stream_logger (stack buffer, pluggable writer).
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <type_traits>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/logging.hpp>
#include <nexenne/logging/stream_logger.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace lg = nexenne::logging;

[[nodiscard]] auto open_temp(std::filesystem::path const& path) -> std::FILE* {
  std::filesystem::remove(path);
  return std::fopen(path.string().c_str(), "wb");
}

[[nodiscard]] auto read_all(std::filesystem::path const& path) -> std::string {
  auto in{std::ifstream{path}};
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

TEST_CASE("nexenne::logging::stream_logger writes a formatted line to a FILE*") {
  auto const path{
    std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / "nexenne_stream_logger_basic.log"
  };
  auto* const f{open_temp(path)};
  REQUIRE(f != nullptr);
  {
    lg::stream_logger log{"dev", lg::level::trace, lg::file_writer{f}};
    log.info("value={}", 7);
  }
  nexenne::utility::ignore(std::fclose(f));

  auto const s{read_all(path)};
  CHECK(s.find("[INFO ]") != std::string::npos);
  CHECK(s.find("[dev]") != std::string::npos);
  CHECK(s.find("-- value=7") != std::string::npos);
  CHECK(s.find("test_stream_logger.cpp:") != std::string::npos);
  CHECK(!s.empty());
  CHECK(s.back() == '\n');
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::stream_logger truncates an overlong message with an ellipsis") {
  auto const path{
    std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / "nexenne_stream_logger_trunc.log"
  };
  auto* const f{open_temp(path)};
  REQUIRE(f != nullptr);
  {
    lg::basic_stream_logger<lg::file_writer, 64> log{"x", lg::level::trace, lg::file_writer{f}};
    log.info("{}", std::string(200, 'A'));
  }
  nexenne::utility::ignore(std::fclose(f));

  auto const s{read_all(path)};
  CHECK(s.size() <= 64);
  CHECK(s.find("...") != std::string::npos);
  std::filesystem::remove(path);
}

TEST_CASE("nexenne::logging::stream_logger terminates a truncated line, so two do not merge") {
  struct buffer_writer {
    std::string* out;

    auto operator()(std::span<char const> const bytes) const noexcept -> void {
      out->append(bytes.data(), bytes.size());
    }
  };

  std::string captured;
  {
    lg::basic_stream_logger<buffer_writer, 64> log{"x", lg::level::trace, buffer_writer{&captured}};
    log.info("{}", std::string(200, 'A'));
    log.info("{}", std::string(200, 'B'));
  }

  CHECK(std::ranges::count(captured, '\n') == 2);
  CHECK(captured.back() == '\n');
  CHECK(captured.find("...") != std::string::npos);
  CHECK(captured.find("A...\n") != std::string::npos);
}

TEST_CASE("nexenne::logging::stream_logger respects the runtime level filter") {
  auto const path{
    std::filesystem::path{NEXENNE_LOGGING_TEST_DIR} / "nexenne_stream_logger_filter.log"
  };
  auto* const f{open_temp(path)};
  REQUIRE(f != nullptr);
  {
    lg::stream_logger log{"x", lg::level::trace, lg::file_writer{f}};
    log.set_min_level(lg::level::warn);
    log.info("dropped");
    log.warn("kept");
  }
  nexenne::utility::ignore(std::fclose(f));

  auto const s{read_all(path)};
  CHECK(s.find("dropped") == std::string::npos);
  CHECK(s.find("kept") != std::string::npos);
  std::filesystem::remove(path);
}

TEST_CASE(
  "nexenne::logging::stream_logger with a null file_writer emits nothing and does not crash"
) {
  lg::stream_logger log{"x", lg::level::trace, lg::file_writer{nullptr}};
  log.info("nothing");
  log.error("still nothing");
  log.writer().stream = nullptr;
  log.warn("ignored");
  CHECK(true);
}

TEST_CASE("nexenne::logging::stream_logger drives a custom (non-FILE*) writer") {
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

TEST_CASE("nexenne::logging the umbrella header builds with or without the host sinks") {
  static_assert(std::is_class_v<lg::stream_logger>);
  CHECK(lg::to_token(lg::level::info) == "INFO");
}

TEST_CASE("nexenne::logging::stream_logger never enables level::off") {
  lg::stream_logger const log{"dev", lg::level::off, lg::file_writer{nullptr}};
  CHECK_FALSE(log.enabled(lg::level::off));
}

}  // namespace
