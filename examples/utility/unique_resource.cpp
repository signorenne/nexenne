/**
 * @file
 * @brief Own a handle and release it once, via nexenne::utility::unique_resource.
 */

#include <print>

#include <nexenne/utility/discard.hpp>
#include <nexenne/utility/unique_resource.hpp>

namespace {

// A stand-in for a POSIX-style descriptor API: open returns -1 on failure.
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
    // Own a live handle; the deleter fires exactly once at the end of the block.
    auto file{nexenne::utility::unique_resource{fake_open("/dev/sensor"), closer}};
    std::println("owns: {}", file.owns());
    if (file.owns()) {
      std::println("using fd {}", file.get());
    }
    // closer(fd) runs here.
  }

  {
    // A failed acquisition returns the -1 sentinel. Release ownership so the
    // deleter never runs on a junk handle; this is what
    // make_unique_resource_checked packages into one call.
    auto file{nexenne::utility::unique_resource{-1, closer}};
    if (file.get() == -1) {
      nexenne::utility::discard(file.release());
    }
    std::println("failed-open owns: {}", file.owns());
  }

  {
    // reset() releases the current handle now (not at scope exit), then adopts a
    // new one with the same deleter, so the first fd closes right away.
    auto file{nexenne::utility::unique_resource{fake_open("/dev/a"), closer}};
    file.reset(fake_open("/dev/a-again"));  // closes the first fd immediately
    std::println("owns after reset: {}", file.owns());
    // the second fd closes here, at the end of the block.
  }

  {
    // release() hands the raw handle back and suppresses the deleter, so the
    // caller becomes responsible for closing it.
    auto owned{nexenne::utility::unique_resource{fake_open("/dev/b"), closer}};
    int const raw{owned.release()};
    std::println("released fd {}, owner still owns: {}", raw, owned.owns());
    fake_close(raw);  // we close it ourselves; the guard would not
  }

  return 0;
}
