/**
 * @file
 * @brief Guarantee cleanup of a faux resource with nexenne::utility::defer.
 */

#include <print>
#include <string>

#include <nexenne/utility/defer.hpp>

namespace {

// A stand-in for a C-style resource handle that must be released exactly once.
auto open_connection(std::string const& host) -> int {
  std::println("opening connection to {}", host);
  return 7;  // pretend file descriptor
}

auto close_connection(int const fd) -> void {
  std::println("closing connection (fd {})", fd);
}

auto fetch(std::string const& host, bool const fail_early) -> bool {
  int const fd{open_connection(host)};
  auto const guard{nexenne::utility::defer{[&] { close_connection(fd); }}};

  if (fail_early) {
    std::println("aborting early; guard still releases the handle");
    return false;  // close_connection runs here on the way out
  }

  std::println("transferring data over fd {}", fd);
  return true;  // close_connection runs here too
}

}  // namespace

auto main() -> int {
  std::println("--- successful path ---");
  fetch("example.org", false);
  std::println("--- early-return path ---");
  fetch("example.org", true);
  // Both paths print a matching "closing connection" line.
  return 0;
}
