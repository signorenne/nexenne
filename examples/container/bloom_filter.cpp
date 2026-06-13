/**
 * @file
 * @brief bloom_filter as a seen-URL pre-filter: cheap reject, no false negatives.
 *
 * Sized for a target false-positive rate, the filter answers "have I probably
 * seen this?" in a few bit probes. A negative is certain; a positive is checked
 * against the real store. No per-item storage, no enumeration.
 */

#include <print>
#include <string>

#include <nexenne/container/bloom_filter.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  // Size for 10k expected URLs at a 1% false-positive rate.
  auto seen{cn::bloom_filter<std::string>::with_target_false_positive_rate(10000, 0.01)};
  seen.insert("https://a.example/");
  seen.insert("https://b.example/");

  std::println("filter bits: {}, hashes: {}", seen.bit_count(), seen.hash_count());
  std::println("seen a.example: {}", seen.contains("https://a.example/"));
  std::println("seen c.example (never inserted): {}", seen.contains("https://c.example/"));
  std::println("estimated false-positive rate: {:.4f}", seen.false_positive_rate());
  // filter bits: 95851, hashes: 7
  // seen a.example: true
  // seen c.example (never inserted): false
  // estimated false-positive rate: 0.0000
  return 0;
}
