/**
 * @file
 * @brief Express a non-optional dependency with nexenne::utility::non_null.
 *
 * \c non_null wraps any dereferenceable, null-comparable pointer, raw or smart.
 * Constructing one from the literal nullptr does not compile, and a runtime
 * null asserts at the construction site in debug builds, so a function taking a
 * \c non_null uses the pointer with no defensive check. The example:
 *
 *   1. passes a raw logger pointer to a job whose signature makes the logger
 *      mandatory;
 *   2. wraps a \c std::shared_ptr, which \c operator-> observes without touching
 *      its reference count;
 *   3. hands a \c std::unique_ptr to a sink that takes ownership, then shows the
 *      moved-from hole: the source holds null, its accessors assert in debug,
 *      comparing with nullptr is the one safe probe, and reassignment restores
 *      the invariant;
 *   4. compares a live \c non_null with nullptr and with a plain pointer.
 */

#include <memory>
#include <print>
#include <string>
#include <utility>

#include <nexenne/utility/non_null.hpp>

namespace util = nexenne::utility;

namespace {

struct logger {
  std::string prefix;

  auto write(std::string const& message) const -> void {
    std::println("{}: {}", prefix, message);
  }
};

auto run_job(util::non_null<logger const*> log, int const items) -> void {
  log->write(std::format("starting job with {} items", items));
  for (int i{0}; i < items; ++i) {
    log->write(std::format("processed item {}", i));
  }
  log->write("job complete");
}

auto greet(util::non_null<std::shared_ptr<logger>> log) -> void {
  log->write("hello from a shared_ptr the wrapper merely guarantees is set");
}

auto adopt(util::non_null<std::unique_ptr<logger>> log) -> void {
  log->write("ownership received, still guaranteed non-null");
}

}  // namespace

auto main() -> int {
  logger const log{"worker"};
  run_job(&log, 2);

  auto shared{std::make_shared<logger>(logger{"async"})};
  greet(shared);
  std::println("refcount intact: {}", shared.use_count());

  util::non_null<std::unique_ptr<logger>> owner{std::make_unique<logger>(logger{"owned"})};
  adopt(std::move(owner));
  std::println("owner moved from: {}", owner == nullptr);
  owner = std::make_unique<logger>(logger{"replacement"});
  owner->write("reassigned, the invariant holds again");

  util::non_null<logger const*> const dep{&log};
  std::println("dep == nullptr: {}", dep == nullptr);
  std::println("dep aliases &log: {}", dep == &log);

  return 0;
}
