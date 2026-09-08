/**
 * @file
 * @brief Own a handle and release it once, via nexenne::utility::unique_resource.
 *
 * A stand-in for a POSIX-style descriptor API, where open returns -1 on
 * failure, drives four scopes:
 *
 *   1. a live handle whose deleter fires exactly once at the end of the block;
 *   2. a failed acquisition: ownership of the -1 sentinel is released so the
 *      deleter never runs on a junk handle, which is what
 *      \c make_unique_resource_checked packages into one call;
 *   3. \c reset closes the first descriptor right away, then adopts a second
 *      one with the same deleter, closed at the end of the block;
 *   4. \c release hands the raw handle back and suppresses the deleter, so the
 *      caller closes it.
 */

#include <print>

#include <nexenne/utility/ignore.hpp>
#include <nexenne/utility/unique_resource.hpp>

namespace {

auto fake_open(char const* name) noexcept -> int {
  static int next_fd{3};
  int const fd{next_fd++};
  std::println("open({}) -> fd {}", name, fd);
  return fd;
}

auto fake_close(int const fd) noexcept -> void {
  std::println("close(fd {})", fd);
}

}  // namespace

auto main() -> int {
  auto const closer{[](int const fd) noexcept { fake_close(fd); }};

  {
    auto file{nexenne::utility::unique_resource{fake_open("/dev/sensor"), closer}};
    std::println("owns: {}", file.owns());
    if (file.owns()) {
      std::println("using fd {}", file.get());
    }
  }

  {
    auto file{nexenne::utility::unique_resource{-1, closer}};
    if (file.get() == -1) {
      nexenne::utility::ignore(file.release());
    }
    std::println("failed-open owns: {}", file.owns());
  }

  {
    auto file{nexenne::utility::unique_resource{fake_open("/dev/a"), closer}};
    file.reset(fake_open("/dev/a-again"));
    std::println("owns after reset: {}", file.owns());
  }

  {
    auto owned{nexenne::utility::unique_resource{fake_open("/dev/b"), closer}};
    int const raw{owned.release()};
    std::println("released fd {}, owner still owns: {}", raw, owned.owns());
    fake_close(raw);
  }

  return 0;
}
