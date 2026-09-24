/**
 * @file
 * @brief reactor: an epoll event loop that receives, decodes, and periodically sends CAN.
 *
 * This is how a real Linux application consumes CAN without blocking or spinning:
 * a single-threaded event loop (a reactor) built on \c epoll. The CAN socket runs
 * non-blocking and sits in the epoll set next to a \c timerfd that drives periodic
 * transmits and a \c signalfd that delivers Ctrl-C, so the loop sleeps in
 * \c epoll_wait until something happens, then drains the socket, sends a frame, or
 * shuts down. Real applications add their other descriptors (a UI pipe, a TCP
 * socket, more timers) to the same set, so the whole app stays responsive on one
 * thread with no polling and no locks.
 *
 * It runs until you stop it with Ctrl-C and prints every frame it receives,
 * decoding the ones in its database, so you can inject traffic and watch it react:
 *
 *   sudo modprobe vcan
 *   sudo ip link add dev vcan0 type vcan
 *   sudo ip link set up vcan0
 *   # then, in another terminal, while this runs:
 *   cansend vcan0 200#0102030405060708
 *   candump vcan0
 *
 * It showcases the repo together: \c nexenne::can for the SocketCAN backend,
 * database, codec, and registry, and \c nexenne::utility::unique_resource to own
 * the epoll, timer, and signal descriptors with RAII so they are always closed.
 * If the interface is missing (or this is not Linux) it reports why and exits
 * cleanly, so it is always safe to run.
 *
 * Details worth copying from main():
 *
 * - SIGINT and SIGTERM are blocked, then delivered through a \c signalfd, so
 *   shutdown is one more readable descriptor in the loop rather than an async
 *   handler.
 * - \c make_unique_resource_checked owns each descriptor and closes it on every
 *   exit path, skipping the deleter on the -1 sentinel a failed call returns.
 * - The timer fires every \c heartbeat_ms to send an engine frame whose rpm and
 *   temperature cycle, so values keep changing; \c receive_own_messages makes
 *   those sends show up in the receive path too.
 * - Each socket wakeup drains every buffered frame, and a decoded signal is
 *   printed only when it is new or its value changed.
 */

#if defined(__linux__)

#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/can/codec.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/socketcan_bus.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/registry.hpp>
#include <nexenne/can/signal_builder.hpp>
#include <nexenne/can/socket_options.hpp>
#include <nexenne/utility/ignore.hpp>
#include <nexenne/utility/unique_resource.hpp>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace {

namespace nc = nexenne::can;
namespace nu = nexenne::utility;

constexpr std::uint16_t engine_id{0x100};  ///< Identifier of the engine message.
constexpr int heartbeat_ms{1000};          ///< Period of the simulated engine ECU, in ms.

/**
 * @brief Builds the database the reactor decodes received frames against.
 *
 * @return A database holding the engine message.
 *
 * @pre None.
 * @post None.
 */
auto build_database() -> nc::database {
  auto const rpm{nc::signal_builder{}
                   .name("engine_rpm")
                   .start_bit(0)
                   .length(16)
                   .endianness(nc::byte_order::little_endian)
                   .scale(0.25)
                   .unit("rpm")
                   .build()};
  auto const temp{nc::signal_builder{}
                    .name("coolant_temp")
                    .start_bit(16)
                    .length(8)
                    .endianness(nc::byte_order::little_endian)
                    .offset(-40.0)
                    .unit("degC")
                    .build()};
  auto const running{nc::signal_builder{}
                       .name("engine_running")
                       .start_bit(24)
                       .length(1)
                       .endianness(nc::byte_order::little_endian)
                       .build()};
  return nc::database_builder{}
    .add_message(
      nc::message_builder{nc::can_id::standard(engine_id), "engine"}
        .add(rpm)
        .add(temp)
        .add(running)
        .build()
    )
    .build();
}

/**
 * @brief Encodes named values into a frame and sends it, the way a periodic ECU would.
 *
 * @param bus Bus to send on.
 * @param msg Message whose signals the values name.
 * @param values Signal names and physical values to encode.
 *
 * @pre None.
 * @post One frame has been sent, or the failure has been printed.
 */
auto transmit(
  nc::socketcan_bus& bus,
  nc::message const& msg,
  std::initializer_list<std::pair<std::string_view, double>> const values
) -> void {
  std::array<std::byte, 8> const zeros{};
  auto built{nc::frame::classic(msg.id(), zeros)};
  if (!built) {
    return;
  }
  for (auto const& [name, value] : values) {
    for (nc::signal_entry const& entry : msg.signals()) {
      if (entry.definition.name() == name) {
        nu::ignore(nc::encode(entry.definition, entry.plan, *built, value));
      }
    }
  }
  if (auto const sent{bus.send(*built)}; !sent) {
    std::println("  send failed: {}", nc::to_string(sent.error()));
  }
}

}  // namespace

auto main() -> int {
  auto const db{build_database()};
  nc::registry const reg{db};
  nc::message const* const engine{reg.find(nc::can_id::standard(engine_id))};

  nc::socket_options options;
  options.nonblocking = true;
  options.receive_own_messages = true;
  auto bus{nc::socketcan_bus::open("vcan0", options)};
  if (!bus) {
    std::println(
      "socketcan unavailable ({}); set up vcan0 to run this for real", nc::to_string(bus.error())
    );
    return 0;
  }

  // Block first, or Ctrl-C still kills the process before the signalfd sees it.
  sigset_t mask{};
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  nu::ignore(::sigprocmask(SIG_BLOCK, &mask, nullptr));

  auto const closer{[](int fd) { nu::ignore(::close(fd)); }};
  auto epoll{nu::make_unique_resource_checked(::epoll_create1(EPOLL_CLOEXEC), -1, closer)};
  auto timer{nu::make_unique_resource_checked(
    ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC), -1, closer
  )};
  auto signals{
    nu::make_unique_resource_checked(::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC), -1, closer)
  };
  if (epoll.get() < 0 || timer.get() < 0 || signals.get() < 0) {
    std::println("failed to create epoll/timer/signal descriptors");
    return 1;
  }

  itimerspec const period{
    .it_interval = {.tv_sec = 0, .tv_nsec = heartbeat_ms * 1'000'000},
    .it_value = {.tv_sec = 0, .tv_nsec = heartbeat_ms * 1'000'000},
  };
  nu::ignore(::timerfd_settime(timer.get(), 0, &period, nullptr));

  auto const watch{[&](int fd) {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    return ::epoll_ctl(epoll.get(), EPOLL_CTL_ADD, fd, &ev) == 0;
  }};
  if (!watch(bus->native_handle()) || !watch(timer.get()) || !watch(signals.get())) {
    std::println("epoll_ctl failed");
    return 1;
  }

  std::println("reactor running on vcan0 (Ctrl-C to stop)");
  std::println("inject from another terminal, e.g. cansend vcan0 200#0102030405060708");

  std::map<std::string, double> last_value{};
  int beats{0};
  bool running{true};
  std::array<epoll_event, 8> events{};

  while (running) {
    int const ready{::epoll_wait(epoll.get(), events.data(), static_cast<int>(events.size()), -1)};
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::println("epoll_wait failed");
      break;
    }

    for (int i{0}; i < ready; ++i) {
      int const fd{events[static_cast<std::size_t>(i)].data.fd};

      if (fd == signals.get()) {
        signalfd_siginfo info{};
        nu::ignore(::read(fd, &info, sizeof(info)));
        std::println("\nsignal {} received, shutting down", info.ssi_signo);
        running = false;
      } else if (fd == timer.get()) {
        std::uint64_t expirations{};
        nu::ignore(::read(fd, &expirations, sizeof(expirations)));
        ++beats;
        if (engine != nullptr) {
          transmit(
            *bus,
            *engine,
            {{"engine_rpm", 800.0 + (beats % 50) * 100.0},
             {"coolant_temp", 80.0 + (beats % 40)},
             {"engine_running", 1.0}}
          );
        }
      } else if (fd == bus->native_handle()) {
        while (true) {
          auto const received{bus->receive()};
          if (!received) {
            std::println("receive failed: {}", nc::to_string(received.error()));
            break;
          }
          if (!received->has_value()) {
            break;
          }
          nc::frame const& f{**received};
          nc::message const* const msg{reg.match(f)};
          if (msg == nullptr) {
            std::println("rx {} (no database entry)", f);
            continue;
          }
          std::println("rx {} [{}]", msg->name(), f);
          reg.decode_signals(f, [&](nc::signal const& sig, double const value) {
            std::string key{std::string{sig.name()}};
            auto const it{last_value.find(key)};
            bool const fresh{it == last_value.end()};
            bool const changed{!fresh && it->second != value};
            last_value[key] = value;
            if (fresh || changed) {
              std::println(
                "   {} = {:g} {} ({})", sig.name(), value, sig.unit(), fresh ? "new" : "changed"
              );
            }
          });
        }
      }
    }
  }

  std::println("reactor stopped");
  return 0;
}

#else

#include <print>

auto main() -> int {
  std::println("reactor: requires Linux SocketCAN (epoll + a vcan/can interface)");
  return 0;
}

#endif
