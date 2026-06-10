/**
 * @file
 * @brief Roll back a partial transaction with nexenne::utility::scope_guard.
 */

#include <print>
#include <vector>

#include <nexenne/utility/scope_guard.hpp>

namespace {

// Append all items to the log, but only keep them if every item is valid.
// The guard rolls back the appended rows unless we dismiss it on success.
auto commit_batch(std::vector<int>& log, std::vector<int> const& batch) -> bool {
  auto const mark{log.size()};
  auto rollback{nexenne::utility::scope_guard{[&] {
    log.resize(mark);
    std::println("rolled back to {} row(s)", mark);
  }}};

  for (int const value : batch) {
    if (value < 0) {
      std::println("invalid value {}; aborting batch", value);
      return false;  // rollback fires here
    }
    log.push_back(value);
  }

  rollback.dismiss();  // success: keep the appended rows
  std::println("committed {} row(s)", batch.size());
  return true;
}

}  // namespace

auto main() -> int {
  std::vector<int> log{1, 2};
  commit_batch(log, {3, 4});   // committed 2 row(s)
  commit_batch(log, {5, -1});  // invalid value -1; rolled back to 4 row(s)
  std::println("final log size: {}", log.size());
  // final log size: 4
  return 0;
}
