/**
 * @file
 * @brief Express a non-optional dependency with nexenne::utility::non_null.
 *
 * non_null wraps any dereferenceable, null-comparable pointer (raw OR smart):
 *   - constructing from nullptr is a compile error;
 *   - a runtime null asserts at the construction site in debug builds;
 *   - the body then uses the pointer with no defensive check.
 */

#include <memory>
#include <print>
#include <string>

#include <nexenne/utility/non_null.hpp>

namespace util = nexenne::utility;

namespace {

struct logger {
  std::string prefix;

  auto write(std::string const& message) const -> void {
    std::println("{}: {}", prefix, message);
  }
};

// The signature documents that a logger is mandatory: passing nullptr will not
// compile, and the body needs no defensive null check before using it.
auto run_job(util::non_null<logger const*> log, int const items) -> void {
  log->write(std::format("starting job with {} items", items));
  for (int i{0}; i < items; ++i) {
    log->write(std::format("processed item {}", i));
  }
  log->write("job complete");
}

// non_null also wraps smart pointers. operator-> forwards through the handle's
// own operator->, so a shared_ptr is observed without churning its refcount.
auto greet(util::non_null<std::shared_ptr<logger>> log) -> void {
  log->write("hello from a shared_ptr the wrapper merely guarantees is set");
}

}  // namespace

auto main() -> int {
  logger const log{"worker"};
  run_job(&log, 2);  // implicit conversion from logger const*

  auto shared{std::make_shared<logger>(logger{"async"})};
  greet(shared);  // a mandatory, never-null shared dependency
  std::println("refcount intact: {}", shared.use_count());

  // The non-null contract is also a fact you can compare against: a non_null is
  // never equal to nullptr, by definition.
  util::non_null<logger const*> const dep{&log};
  std::println("dep == nullptr: {}", dep == nullptr);
  std::println("dep aliases &log: {}", dep.get() == &log);

  // util::non_null<logger const*>{nullptr};  // ERROR: deleted nullptr ctor
  return 0;
}
