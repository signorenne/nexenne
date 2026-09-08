/**
 * @file
 * @brief Roll back a partial transaction with nexenne::utility::scope_guard.
 *
 * \c commit_batch appends every item to a log but keeps them only if all are
 * valid: a guard armed before the first append truncates the log back to its
 * mark on every exit path, and is dismissed once the whole batch succeeded.
 * The first batch commits; the second hits a negative value and is rolled back,
 * leaving four rows.
 */

#include <print>
#include <vector>

#include <nexenne/utility/scope_guard.hpp>

namespace {

auto commit_batch(std::vector<int>& log, std::vector<int> const& batch) -> bool {
  auto const mark{log.size()};
  auto rollback{nexenne::utility::scope_guard{[&] {
    log.resize(mark);
    std::println("rolled back to {} row(s)", mark);
  }}};

  for (int const value : batch) {
    if (value < 0) {
      std::println("invalid value {}; aborting batch", value);
      return false;
    }
    log.push_back(value);
  }

  rollback.dismiss();
  std::println("committed {} row(s)", batch.size());
  return true;
}

}  // namespace

auto main() -> int {
  std::vector<int> log{1, 2};
  commit_batch(log, {3, 4});
  commit_batch(log, {5, -1});
  std::println("final log size: {}", log.size());
  return 0;
}
