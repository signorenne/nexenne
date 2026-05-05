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

// The API boundary: the signature documents that a logger is mandatory.
// Passing nullptr will not compile, a runtime null asserts here (in debug) at
// the call site, and the body needs no defensive null check before using it.
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

// A sink that takes ownership: non_null<unique_ptr> says "hand me a live
// logger", so even an ownership transfer never needs a null check inside.
auto adopt(util::non_null<std::unique_ptr<logger>> log) -> void {
  log->write("ownership received, still guaranteed non-null");
}

}  // namespace

auto main() -> int {
  logger const log{"worker"};
  run_job(&log, 2);  // implicit conversion from logger const*

  auto shared{std::make_shared<logger>(logger{"async"})};
  greet(shared);  // a mandatory, never-null shared dependency
  std::println("refcount intact: {}", shared.use_count());

  // WARNING: the moved-from hole. Moving a non_null over a move-only handle
  // (unique_ptr here) leaves the source wrapper holding null. The invariant is
  // suspended: the accessors (get, ->, *, the conversion) assert in debug from
  // this point, and the only valid operations left are destruction and
  // reassignment. Comparing against nullptr is the one honest, non-asserting
  // probe of that state.
  util::non_null<std::unique_ptr<logger>> owner{std::make_unique<logger>(logger{"owned"})};
  adopt(std::move(owner));
  std::println("owner moved from: {}", owner == nullptr);  // true: do not touch it
  owner = std::make_unique<logger>(logger{"replacement"});  // reassignment restores it
  owner->write("reassigned, the invariant holds again");

  // The non-null contract is also a fact you can compare against: a live
  // non_null is never equal to nullptr, and it compares directly with a plain
  // pointer of the wrapped type.
  util::non_null<logger const*> const dep{&log};
  std::println("dep == nullptr: {}", dep == nullptr);
  std::println("dep aliases &log: {}", dep == &log);

  // util::non_null<logger const*>{nullptr};  // ERROR: deleted nullptr ctor
  return 0;
}
