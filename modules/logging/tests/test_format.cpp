/**
 * @file
 * @brief Tests for the nexenne::logging formatting layers.
 */

#include <doctest/doctest.h>

#include <cstdio>
#include <format>
#include <memory>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#include <nexenne/logging/format.hpp>

namespace {

namespace lg = nexenne::logging;

// Checks that the formatter, to_string and operator<< all print the name.
template <typename E>
auto names_agree(E const value, std::string_view const name) -> bool {
  auto os{std::ostringstream{}};
  os << value;
  return lg::to_string(value) == name && std::format("{}", value) == name && os.str() == name;
}

/**
 * @brief Checks that \c std::format, \c to_string and \c operator<< all print \p text.
 *
 * @tparam T Type under test, printable through the logging layers.
 * @param value Value to print.
 * @param text Expected one-line summary.
 *
 * @pre None.
 * @post One doctest check per layer has been recorded.
 */
template <typename T>
auto check_layers(T const& value, std::string_view const text) -> void {
  auto os{std::ostringstream{}};
  os << value;
  CHECK(std::format("{}", value) == text);
  CHECK(lg::to_string(value) == text);
  CHECK(os.str() == text);
}

/**
 * @brief Stream-logger writer that discards its bytes and has no formatter.
 *
 * @pre None.
 * @post None.
 */
struct discard_writer {
  /**
   * @brief Accepts a formatted line and drops it.
   *
   * @param bytes The formatted line, ignored.
   *
   * @pre None.
   * @post None.
   */
  auto operator()([[maybe_unused]] std::span<char const> const bytes) const noexcept -> void {}
};

TEST_CASE("nexenne::logging::level prints its padded name through every layer") {
  CHECK(names_agree(lg::level::info, "INFO "));
  CHECK(names_agree(lg::level::critical, "CRIT "));
  CHECK(std::format("[{:>6}]", lg::level::warn) == "[ WARN ]");
  static_assert(lg::to_string(lg::level::error) == "ERROR");
}

TEST_CASE("nexenne::logging::record prints its level, logger, call site and message") {
  auto r{lg::record{}};
  r.severity = lg::level::warn;
  r.logger_name = "net";
  r.message = "link \"eth0\"\ndown";
  check_layers(
    r, R"x(record(level=WARN, logger=net, location=:0, message="link \"eth0\"\ndown"))x"
  );

  auto const here{std::source_location::current()};
  auto const stamped{lg::record{lg::level::info, here, "app", "ready"}};
  CHECK(
    std::format("{}", stamped)
    == std::format(
      R"x(record(level=INFO, logger=app, location={}:{}, message="ready"))x",
      here.file_name(),
      here.line()
    )
  );
}

TEST_CASE("nexenne::logging::config prints its queue size and dispatch mode") {
  check_layers(lg::config<256, true>{}, "config(queue_size=256, async=true)");
  CHECK(std::format("{:>35}", lg::config<1, false>{}) == "  config(queue_size=1, async=false)");
}

TEST_CASE("nexenne::logging::format_string prints its spec and call site") {
  auto const fs{lg::format_string<int>{"x={}"}};
  check_layers(
    fs,
    std::format(
      R"x(format_string(fmt="x={{}}", location={}:{}))x", fs.loc.file_name(), fs.loc.line()
    )
  );
}

TEST_CASE("nexenne::logging stream loggers print their name, level, buffer and writer") {
  check_layers(lg::file_writer{}, "file_writer(stdout)");
  CHECK(lg::to_string(lg::file_writer{stderr}) == "file_writer(stderr)");
  CHECK(lg::to_string(lg::file_writer{nullptr}) == "file_writer(null)");

  auto const uart{lg::stream_logger{"uart", lg::level::info, lg::file_writer{nullptr}}};
  check_layers(
    uart,
    "basic_stream_logger(name=uart, min_level=INFO, buffer_size=256, writer=file_writer(null))"
  );

  auto const quiet{lg::basic_stream_logger<discard_writer, 64>{"quiet"}};
  check_layers(quiet, "basic_stream_logger(name=quiet, min_level=TRACE, buffer_size=64)");
}

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS)

/**
 * @brief A path no sink can open, so a test never creates a file.
 *
 * Its parent is this source file, which is not a directory, so \c std::fopen
 * fails before touching the filesystem.
 *
 * @return The unopenable path.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] auto unopenable_path() -> std::string {
  return std::string{__FILE__} + "/unopenable.log";
}

TEST_CASE("nexenne::logging host enums print their names through every layer") {
  CHECK(names_agree(lg::overflow_action::block, "block"));
  CHECK(names_agree(lg::overflow_action::drop_oldest, "drop_oldest"));
  CHECK(names_agree(lg::overflow_action::drop_newest, "drop_newest"));
  CHECK(names_agree(lg::console_sink::stream::stdout_only, "stdout_only"));
  CHECK(names_agree(lg::console_sink::stream::stderr_only, "stderr_only"));
  CHECK(names_agree(lg::console_sink::stream::auto_split, "auto_split"));
}

TEST_CASE("nexenne::logging::async_sink_config prints its bound and overflow policy") {
  check_layers(
    lg::async_sink_config{}, "async_sink_config(queue_size_limit=1024, on_overflow=block)"
  );
  CHECK(
    std::format("{}", lg::async_sink_config{8, lg::overflow_action::drop_oldest})
    == "async_sink_config(queue_size_limit=8, on_overflow=drop_oldest)"
  );
}

TEST_CASE("nexenne::logging sinks print their target and minimum level") {
  auto console{lg::console_sink{lg::console_sink::stream::stderr_only}};
  console.set_min_level(lg::level::warn);
  check_layers(console, "console_sink(routing=stderr_only, min_level=WARN)");
  check_layers(static_cast<lg::sink const&>(console), "sink(min_level=WARN)");

  auto const file{lg::file_sink{unopenable_path()}};
  check_layers(file, "file_sink(closed, min_level=TRACE)");

  auto ring{lg::ring_sink<4>{}};
  ring.write(lg::record{});
  ring.write(lg::record{});
  check_layers(ring, "ring_sink(size=2, capacity=4, min_level=TRACE)");

  auto const json{lg::json_sink{stdout}};
  check_layers(json, "json_sink(open, min_level=TRACE)");
  auto const json_closed{lg::json_sink{static_cast<std::FILE*>(nullptr)}};
  CHECK(std::format("{}", json_closed) == "json_sink(closed, min_level=TRACE)");

  auto fan{lg::multi_sink{}};
  fan.add(std::make_unique<lg::ring_sink<2>>());
  fan.add(std::make_unique<lg::console_sink>());
  fan.set_min_level(lg::level::info);
  check_layers(fan, "multi_sink(children=2, min_level=INFO)");

  auto const path{unopenable_path()};
  auto const rotating{lg::rotating_file_sink{path, 4096, 3}};
  check_layers(
    rotating,
    std::format(
      "rotating_file_sink(path={}, closed, size=0, max_bytes=4096, max_files=3, min_level=TRACE)",
      path
    )
  );
}

TEST_CASE("nexenne::logging::async_sink prints its queue bound, overflow policy and level") {
  auto async{lg::async_sink{
    std::make_unique<lg::ring_sink<2>>(),
    lg::async_sink_config{16, lg::overflow_action::drop_newest}
  }};
  async.set_min_level(lg::level::error);
  check_layers(async, "async_sink(queue_size_limit=16, on_overflow=drop_newest, min_level=ERROR)");
}

TEST_CASE("nexenne::logging::pattern_formatter prints its pattern quoted and escaped") {
  check_layers(
    lg::pattern_formatter{}, R"x(pattern_formatter(pattern="[%T] [%L] [%n] %m (%f:%#)"))x"
  );
  auto const tabbed{lg::pattern_formatter{"%l\t%m"}};
  CHECK(std::format("{}", tabbed) == R"x(pattern_formatter(pattern="%l\t%m"))x");
}

TEST_CASE("nexenne::logging managers print their sink count") {
  auto& sync_mgr{lg::basic_manager<lg::config<2, false>>::instance()};
  sync_mgr.add_sink(std::make_shared<lg::ring_sink<2>>());
  check_layers(sync_mgr, "basic_sync_manager(sinks=1)");
  sync_mgr.clear_sinks();

  auto& async_mgr{lg::basic_manager<lg::config<32, true>>::instance()};
  check_layers(async_mgr, "basic_async_manager(queue_size=32, sinks=0, dropped=0)");
  async_mgr.shutdown();
}

TEST_CASE("nexenne::logging::basic_logger prints its name and minimum level") {
  auto const net{lg::basic_logger<lg::config<2, false>>{"net", lg::level::info}};
  check_layers(net, "basic_logger(name=net, min_level=INFO)");
}

#endif

}  // namespace
