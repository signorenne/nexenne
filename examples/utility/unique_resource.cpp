/**
 * @file
 * @brief Own a handle and release it once, via nexenne::utility::unique_resource.
 */

#include <print>

#include <nexenne/utility/unique_resource.hpp>

namespace {

// A stand-in for a POSIX-style descriptor API: open returns -1 on failure.
auto fake_open(char const* name) noexcept -> int {
  std::println("open({}) -> fd 3", name);
  return 3;
}

auto fake_close(int const fd) noexcept -> void {
  std::println("close(fd {})", fd);
}

}  // namespace

auto main() -> int {
  auto const closer{[](int const fd) noexcept { fake_close(fd); }};

  // -1 is the invalid sentinel: a failed open would leave the owner empty.
  auto file{nexenne::utility::make_unique_resource_checked(fake_open("/dev/sensor"), -1, closer)};

  std::println("owns: {}", file.owns());
  if (file.owns()) {
    std::println("using fd {}", file.get());
  }

  // A failed acquisition: sentinel matches, so the deleter never runs.
  auto bad{nexenne::utility::make_unique_resource_checked(-1, -1, closer)};
  std::println("bad owns: {}", bad.owns());

  // file's deleter fires here at scope exit.
  return 0;
}
