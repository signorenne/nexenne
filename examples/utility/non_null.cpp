/**
 * @file
 * @brief Express a non-optional dependency with nexenne::utility::non_null.
 */

#include <print>
#include <string>

#include <nexenne/utility/non_null.hpp>

namespace {

struct logger {
  std::string prefix;

  auto write(std::string const& message) const -> void {
    std::println("{}: {}", prefix, message);
  }
};

// The signature documents that a logger is mandatory: passing nullptr will not
// compile, and the body needs no defensive null check before using it.
auto run_job(nexenne::utility::non_null<logger const*> log, int const items) -> void {
  log->write(std::format("starting job with {} items", items));
  for (int i{0}; i < items; ++i) {
    log->write(std::format("processed item {}", i));
  }
  log->write("job complete");
}

}  // namespace

auto main() -> int {
  logger const log{"worker"};
  run_job(&log, 2);  // implicit conversion from logger const*
  // worker: starting job with 2 items
  // worker: processed item 0
  // worker: processed item 1
  // worker: job complete
  return 0;
}
