/**
 * @file
 * @brief bloom_filter as a seen-URL pre-filter: cheap reject, no false negatives.
 *
 * Sized for a target false-positive rate, the filter answers "have I probably
 * seen this?" in a few bit probes. A negative is certain; a positive is checked
 * against the real store. No per-item storage, no enumeration. The factory sizes
 * the filter for 10k URLs at a 1% rate and returns a result: a zero item count
 * or a rate outside (0, 1) is rejected.
 *
 * Expected output:
 *
 * \code
 * filter bits: 95851, hashes: 7
 * seen a.example: true
 * seen c.example (never inserted): false
 * estimated false-positive rate: 0.0000
 * \endcode
 */

#include <print>
#include <string>

#include <nexenne/container/bloom_filter.hpp>
#include <nexenne/container/error.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  auto made{cn::bloom_filter<std::string>::with_target_false_positive_rate(10000, 0.01)};
  if (!made.has_value()) {
    std::println("bad sizing: {}", cn::to_string(made.error()));
    return 1;
  }
  auto& seen{*made};
  seen.insert("https://a.example/");
  seen.insert("https://b.example/");

  std::println("filter bits: {}, hashes: {}", seen.bit_count(), seen.hash_count());
  std::println("seen a.example: {}", seen.contains("https://a.example/"));
  std::println("seen c.example (never inserted): {}", seen.contains("https://c.example/"));
  std::println("estimated false-positive rate: {:.4f}", seen.false_positive_rate());
  return 0;
}
