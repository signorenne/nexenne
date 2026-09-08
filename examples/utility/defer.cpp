/**
 * @file
 * @brief Place cleanup next to acquisition with nexenne::utility::defer.
 *
 * Stand-ins for C-style connection and lock APIs, which have no RAII of their
 * own, are paired with a \c defer written on the line right after each
 * acquisition, so no exit path can forget it. Guards run in reverse order of
 * declaration, so the region unlocks before the connection closes, mirroring
 * the acquisition order. Both the successful and the early-return path print a
 * matching unlock then close pair.
 */

#include <print>
#include <string>

#include <nexenne/utility/defer.hpp>

namespace {

auto open_connection(std::string const& host) -> int {
  std::println("open connection to {}", host);
  return 7;
}

auto close_connection(int const fd) -> void {
  std::println("close connection (fd {})", fd);
}

auto lock_region() -> void {
  std::println("lock region");
}

auto unlock_region() -> void {
  std::println("unlock region");
}

auto fetch(std::string const& host, bool const fail_early) -> bool {
  int const fd{open_connection(host)};
  auto const closer{nexenne::utility::defer{[&] { close_connection(fd); }}};

  lock_region();
  auto const unlocker{nexenne::utility::defer{[&] { unlock_region(); }}};

  if (fail_early) {
    std::println("abort early; both guards still run on the way out");
    return false;
  }

  std::println("transfer data over fd {}", fd);
  return true;
}

}  // namespace

auto main() -> int {
  std::println("--- successful path ---");
  bool const ok{fetch("example.org", false)};
  std::println("--- early-return path ---");
  bool const aborted{fetch("example.org", true)};
  std::println("results: {} then {}", ok, aborted);
  return 0;
}
