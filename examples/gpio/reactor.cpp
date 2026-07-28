/**
 * @file
 * @brief reactor: drive GPIO from an external event loop, the Qt-shaped way.
 *
 * The integration pattern every framework uses: the backend hands out its
 * request file descriptor through \c native_handle(), the loop (here epoll,
 * in Qt a \c QSocketNotifier, in ASIO a \c posix::stream_descriptor) waits
 * for readability, and ready events are drained non-blockingly with
 * \c wait_event(0ns). A \c timerfd drives a periodic heartbeat on an output
 * line in the same single thread, so nothing here ever blocks or spins.
 *
 * Pass the chip index, a button offset, and a LED offset (defaults 0, 17,
 * 4). Exits cleanly with a message when no usable hardware is present.
 */

#include <print>

#ifdef __linux__

#  include <array>
#  include <chrono>
#  include <cstdlib>

#  include <sys/epoll.h>
#  include <sys/timerfd.h>
#  include <unistd.h>

#  include <nexenne/gpio/chip.hpp>
#  include <nexenne/gpio/decode.hpp>
#  include <nexenne/gpio/format.hpp>
#  include <nexenne/gpio/io/chardev_chip.hpp>
#  include <nexenne/utility/discard.hpp>
#  include <nexenne/utility/scope_guard.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

}  // namespace

auto main(int const argc, char** const argv) -> int {
  auto const chip_index{
    static_cast<std::uint16_t>(argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 0)
  };
  auto const button_offset{
    static_cast<std::uint32_t>(argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 17)
  };
  auto const led_offset{
    static_cast<std::uint32_t>(argc > 3 ? std::strtoul(argv[3], nullptr, 10) : 4)
  };

  std::array const specs{
    ng::line_spec::input(
      "button", ng::chip_id{chip_index}, ng::line_offset{button_offset},
      ng::line_polarity::active_low, ng::line_bias::pull_up
    ),
    ng::line_spec::output("led", ng::chip_id{chip_index}, ng::line_offset{led_offset}),
  };
  std::array const configs{
    ng::line_config{ng::edge_detection::both, 10ms},
    ng::line_config{},
  };

  ng::chardev_chip backend{ng::chip_id{chip_index}, "nexenne-reactor"};
  ng::chip<ng::chardev_chip> chip{backend};
  if (auto const opened{chip.open(specs, configs)}; !opened.has_value()) {
    std::println(
      "cannot open gpiochip{}: {} (missing hardware, permissions, or busy)", chip_index,
      opened.error()
    );
    return 0;
  }
  auto led{*chip.line_for("led")};

  // One epoll instance watches both descriptors; this is the seam a Qt
  // QSocketNotifier or an ASIO descriptor occupies instead.
  int const epoll_fd{::epoll_create1(EPOLL_CLOEXEC)};
  if (epoll_fd < 0) {
    std::println("epoll_create1 failed");
    return 1;
  }
  auto const close_epoll{nexenne::utility::scope_guard{[epoll_fd]() noexcept {
    ::close(epoll_fd);
  }}};

  int const timer_fd{::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC)};
  if (timer_fd < 0) {
    std::println("timerfd_create failed");
    return 1;
  }
  auto const close_timer{nexenne::utility::scope_guard{[timer_fd]() noexcept {
    ::close(timer_fd);
  }}};
  ::itimerspec period{};
  period.it_interval.tv_nsec = 500'000'000;  // 500ms heartbeat
  period.it_value = period.it_interval;
  if (::timerfd_settime(timer_fd, 0, &period, nullptr) < 0) {
    std::println("timerfd_settime failed");
    return 1;
  }

  ::epoll_event want_gpio{};
  want_gpio.events = EPOLLIN;
  want_gpio.data.fd = backend.native_handle();
  ::epoll_event want_timer{};
  want_timer.events = EPOLLIN;
  want_timer.data.fd = timer_fd;
  if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, backend.native_handle(), &want_gpio) < 0
      || ::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &want_timer) < 0) {
    std::println("epoll_ctl failed");
    return 1;
  }

  std::println("reactor running: heartbeat on line {}, edges from line {} (20 ticks)",
               led_offset, button_offset);
  for (int ticks{0}; ticks < 20;) {
    std::array<::epoll_event, 4> ready{};
    int const count{::epoll_wait(epoll_fd, ready.data(), static_cast<int>(ready.size()), -1)};
    if (count < 0) {
      continue;  // EINTR: just wait again
    }
    for (int i{0}; i < count; ++i) {
      if (ready[static_cast<std::size_t>(i)].data.fd == timer_fd) {
        std::uint64_t expirations{};
        nexenne::utility::discard(::read(timer_fd, &expirations, sizeof(expirations)));
        nexenne::utility::discard(led.toggle());
        ticks += 1;
      } else {
        // Drain every event the readiness covered; never block here.
        while (true) {
          auto const event{backend.wait_event(0ns)};
          if (!event.has_value() || !event->has_value()) {
            break;
          }
          std::println("{}", ng::decode(specs[0], **event));
        }
      }
    }
  }
  return 0;
}

#else

auto main() -> int {
  std::println("the reactor example needs Linux (epoll, timerfd, and the GPIO chardev)");
  return 0;
}

#endif
