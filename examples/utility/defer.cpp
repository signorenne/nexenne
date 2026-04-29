/**
 * @file
 * @brief Place cleanup next to acquisition with nexenne::utility::defer.
 */

#include <print>
#include <string>

#include <nexenne/utility/defer.hpp>

namespace {

// Stand-ins for C-style APIs that have no RAII of their own.
auto open_connection(std::string const& host) -> int {
  std::println("open connection to {}", host);
  return 7;  // pretend file descriptor
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

// Each cleanup is written on the line right after its acquisition, so no exit
// path can forget it. Guards run in reverse order of declaration, so the region
// unlocks before the connection closes, mirroring the acquisition order.
auto fetch(std::string const& host, bool const fail_early) -> bool {
  int const fd{open_connection(host)};
  auto const closer{nexenne::utility::defer{[&] { close_connection(fd); }}};

  lock_region();
  auto const unlocker{nexenne::utility::defer{[&] { unlock_region(); }}};

  if (fail_early) {
    std::println("abort early; both guards still run on the way out");
    return false;  // unlock then close run here
  }

  std::println("transfer data over fd {}", fd);
  return true;  // unlock then close run here too
}

}  // namespace

auto main() -> int {
  std::println("--- successful path ---");
  bool const ok{fetch("example.org", false)};
  std::println("--- early-return path ---");
  bool const aborted{fetch("example.org", true)};
  std::println("results: {} then {}", ok, aborted);
  // Both paths print a matching unlock then close pair.
  return 0;
}
