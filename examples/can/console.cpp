/**
 * @file
 * @brief console: an interactive CAN test terminal built on the epoll reactor.
 *
 * A complete, hands-on testing tool. It is the [[file:reactor.cpp]] pattern with
 * standard input added to the same \c epoll set, so one thread both shows every
 * frame arriving on the bus and reads commands you type to send frames of every
 * kind: Classic and CAN FD, standard and extended identifiers, remote frames, and
 * database-encoded signals. Because the socket opens with \c receive_own_messages,
 * a frame you send loops straight back and is displayed, so you can verify each
 * frame type round-trips correctly without a second node.
 *
 * It showcases the repo together: \c nexenne::can for the SocketCAN backend,
 * frame factories, database, codec, and registry, and
 * \c nexenne::utility::unique_resource for the epoll and timer descriptors.
 *
 * Run it against a virtual interface (no hardware needed):
 *
 *   sudo modprobe vcan
 *   sudo ip link add dev vcan0 type vcan
 *   sudo ip link set up vcan0
 *   ./nexenne_example_can_console
 *
 * Then type "help". External tools interoperate too: candump vcan0, cansend vcan0.
 */

#if defined(__linux__)

#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
#include <nexenne/utility/discard.hpp>
#include <nexenne/utility/unique_resource.hpp>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace {

namespace nc = nexenne::can;
namespace nu = nexenne::utility;

constexpr std::uint16_t engine_id{0x100};
constexpr std::uint32_t standard_id_max{0x7FF};
constexpr int heartbeat_ms{1000};

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
  return nc::database_builder{}
    .add_message(
      nc::message_builder{nc::can_id::standard(engine_id), "engine"}.add(rpm).add(temp).build()
    )
    .build();
}

// Splits a line into whitespace-separated tokens.
auto tokenize(std::string_view line) -> std::vector<std::string_view> {
  std::vector<std::string_view> out{};
  std::size_t i{0};
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
    }
    std::size_t const start{i};
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
      ++i;
    }
    if (i > start) {
      out.push_back(line.substr(start, i - start));
    }
  }
  return out;
}

auto parse_hex_u32(std::string_view text, std::uint32_t& out) -> bool {
  if (text.starts_with("0x") || text.starts_with("0X")) {
    text.remove_prefix(2);
  }
  auto const end{text.data() + text.size()};
  auto const [ptr, ec]{std::from_chars(text.data(), end, out, 16)};
  return ec == std::errc{} && ptr == end;
}

// Parses a run of hex bytes ("11 22 33" or "112233") into a byte vector.
auto parse_payload(std::span<std::string_view const> const tokens) -> std::vector<std::byte> {
  std::string hex{};
  for (std::string_view const token : tokens) {
    hex += token;
  }
  std::vector<std::byte> bytes{};
  for (std::size_t i{0}; i + 2 <= hex.size(); i += 2) {
    std::uint32_t value{0};
    auto const* const start{hex.data() + i};
    auto const [ptr, ec]{std::from_chars(start, start + 2, value, 16)};
    if (ec != std::errc{} || ptr != start + 2) {
      bytes.clear();
      break;
    }
    bytes.push_back(static_cast<std::byte>(value));
  }
  return bytes;
}

auto make_id(std::uint32_t const raw, bool const force_extended) -> nc::can_id {
  if (force_extended || raw > standard_id_max) {
    return nc::can_id::extended(raw);
  }
  return nc::can_id::standard(static_cast<std::uint16_t>(raw));
}

auto print_menu() -> void {
  std::println("commands:");
  std::println("  send <id> <hex>    Classic data frame (id hex; > 7FF becomes extended)");
  std::println("  ext  <id> <hex>    force a 29-bit extended identifier");
  std::println("  fd   <id> <hex>    CAN FD frame, bit-rate switch set (up to 64 bytes)");
  std::println("  rtr  <id>          remote request frame (no data)");
  std::println("  sig  <name> <val>  encode a named engine signal and send it");
  std::println("  filter <id>|clear  set or clear a receive filter");
  std::println("  auto on|off        periodic engine heartbeat (default off)");
  std::println("  state | stats | help | quit");
}

// Renders a received frame: kind, scope, flags, length, bytes, then decoded
// signals for frames the database knows, marking first-seen and changed values.
auto show_frame(
  nc::frame const& f, nc::registry const& reg, std::map<std::string, double>& last_value
) -> void {
  std::string flags{};
  if (f.is_fd() && f.flags().has(nc::fd_flag::brs)) {
    flags += " BRS";
  }
  if (f.id().remote()) {
    flags += " RTR";
  }
  std::string bytes{};
  for (std::byte const b : f.data()) {
    bytes += std::format("{:02X} ", std::to_integer<unsigned>(b));
  }
  std::println(
    "RX  0x{:0{}X}  {}  {}{}  [{}]  {}",
    f.id().identifier(),
    f.id().extended() ? 8 : 3,
    f.is_fd() ? "FD" : "CL",
    f.id().extended() ? "EXT" : "STD",
    flags,
    f.length(),
    bytes
  );

  nc::message const* const msg{reg.match(f)};
  if (msg == nullptr) {
    return;
  }
  reg.decode_signals(f, [&](nc::signal const& sig, double const value) {
    std::string key{std::string{sig.name()}};
    auto const it{last_value.find(key)};
    bool const fresh{it == last_value.end()};
    bool const changed{!fresh && it->second != value};
    last_value[key] = value;
    if (fresh || changed) {
      std::println(
        "    {} = {:g} {} ({})", sig.name(), value, sig.unit(), fresh ? "new" : "changed"
      );
    }
  });
}

}  // namespace

auto main() -> int {
  auto const db{build_database()};
  nc::registry const reg{db};
  nc::message const* const engine{reg.find(nc::can_id::standard(engine_id))};

  nc::socket_options options;
  options.fd_enabled = true;            // allow sending and receiving CAN FD frames
  options.receive_own_messages = true;  // loop our sends back so they are displayed
  options.nonblocking = true;
  auto bus{nc::socketcan_bus::open("vcan0", options)};
  if (!bus) {
    std::println(
      "socketcan unavailable ({}); set up vcan0 to run this for real", nc::to_string(bus.error())
    );
    return 0;
  }

  auto const closer{[](int fd) { nu::discard(::close(fd)); }};
  auto epoll{nu::make_unique_resource_checked(::epoll_create1(EPOLL_CLOEXEC), -1, closer)};
  auto timer{nu::make_unique_resource_checked(
    ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC), -1, closer
  )};
  if (epoll.get() < 0 || timer.get() < 0) {
    std::println("failed to create epoll/timer descriptors");
    return 1;
  }
  itimerspec const period{
    .it_interval = {.tv_sec = 0, .tv_nsec = heartbeat_ms * 1'000'000},
    .it_value = {.tv_sec = 0, .tv_nsec = heartbeat_ms * 1'000'000},
  };
  nu::discard(::timerfd_settime(timer.get(), 0, &period, nullptr));

  auto const watch{[&](int fd) {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    return ::epoll_ctl(epoll.get(), EPOLL_CTL_ADD, fd, &ev) == 0;
  }};
  if (!watch(bus->descriptor()) || !watch(timer.get()) || !watch(STDIN_FILENO)) {
    std::println("epoll_ctl failed");
    return 1;
  }

  std::println("nexenne CAN console on vcan0. type 'help'.");

  std::map<std::string, double> last_value{};
  std::string input{};  // accumulates partial stdin between reads
  std::uint64_t rx_count{0};
  std::uint64_t tx_count{0};
  int beats{0};
  bool auto_tx{false};
  bool running{true};
  std::array<epoll_event, 8> events{};

  auto const send{[&](nc::frame const& f) {
    if (auto const sent{bus->send(f)}; !sent) {
      std::println("send failed: {}", nc::to_string(sent.error()));
    } else {
      ++tx_count;
    }
  }};

  auto const run_command{[&](std::string_view const line) {
    auto const tokens{tokenize(line)};
    if (tokens.empty()) {
      return;
    }
    std::string_view const cmd{tokens[0]};
    auto const rest{std::span{tokens}.subspan(1)};

    if (cmd == "help" || cmd == "?") {
      print_menu();
    } else if (cmd == "quit" || cmd == "q" || cmd == "exit") {
      running = false;
    } else if (cmd == "state") {
      std::println("bus state: {}", bus->state());
    } else if (cmd == "stats") {
      std::println("rx={} tx={} auto={}", rx_count, tx_count, auto_tx ? "on" : "off");
    } else if (cmd == "auto") {
      auto_tx = !rest.empty() && rest[0] == "on";
      std::println("auto heartbeat {}", auto_tx ? "on" : "off");
    } else if (cmd == "filter") {
      if (!rest.empty() && rest[0] == "clear") {
        nu::discard(bus->set_filters({}));
        std::println("filters cleared");
      } else if (std::uint32_t id{0}; !rest.empty() && parse_hex_u32(rest[0], id)) {
        std::array const filters{nc::filter::equals(make_id(id, false))};
        nu::discard(bus->set_filters(filters));
        std::println("filter set to 0x{:X}", id);
      } else {
        std::println("usage: filter <id>|clear");
      }
    } else if (cmd == "sig") {
      if (rest.size() < 2 || engine == nullptr) {
        std::println("usage: sig <name> <value>");
        return;
      }
      double const value{std::strtod(std::string{rest[1]}.c_str(), nullptr)};
      std::array<std::byte, 8> const zeros{};
      auto built{nc::frame::classic(engine->id(), zeros)};
      bool encoded{false};
      for (nc::signal_entry const& entry : engine->signals()) {
        if (entry.definition.name() == rest[0]) {
          encoded = nc::encode(entry.definition, entry.plan, *built, value).has_value();
        }
      }
      if (encoded) {
        send(*built);
      } else {
        std::println("unknown signal '{}'", rest[0]);
      }
    } else if (cmd == "send" || cmd == "ext" || cmd == "fd" || cmd == "rtr") {
      std::uint32_t id{0};
      if (rest.empty() || !parse_hex_u32(rest[0], id)) {
        std::println("usage: {} <id> ...", cmd);
        return;
      }
      auto const payload{parse_payload(rest.subspan(1))};
      if (cmd == "rtr") {
        auto built{nc::frame::classic(make_id(id, false), {})};
        built->id().remote() = true;
        send(*built);
      } else if (cmd == "fd") {
        auto built{
          nc::frame::fd(make_id(id, false), payload, nu::flags<nc::fd_flag>{nc::fd_flag::brs})
        };
        if (built) {
          send(*built);
        } else {
          std::println("bad FD frame: {}", nc::to_string(built.error()));
        }
      } else {
        auto built{nc::frame::classic(make_id(id, cmd == "ext"), payload)};
        if (built) {
          send(*built);
        } else {
          std::println("bad frame: {}", nc::to_string(built.error()));
        }
      }
    } else {
      std::println("unknown command '{}'; type 'help'", cmd);
    }
  }};

  while (running) {
    int const ready{::epoll_wait(epoll.get(), events.data(), static_cast<int>(events.size()), -1)};
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    for (int i{0}; i < ready; ++i) {
      int const fd{events[static_cast<std::size_t>(i)].data.fd};

      if (fd == STDIN_FILENO) {
        std::array<char, 512> buffer{};
        auto const got{::read(STDIN_FILENO, buffer.data(), buffer.size())};
        if (got <= 0) {
          running = false;  // EOF (Ctrl-D) or error ends the session
          break;
        }
        input.append(buffer.data(), static_cast<std::size_t>(got));
        std::size_t newline{0};
        while ((newline = input.find('\n')) != std::string::npos) {
          run_command(std::string_view{input}.substr(0, newline));
          input.erase(0, newline + 1);
        }
      } else if (fd == timer.get()) {
        std::uint64_t expirations{};
        nu::discard(::read(fd, &expirations, sizeof(expirations)));
        ++beats;
        if (auto_tx && engine != nullptr) {
          std::array<std::byte, 8> const zeros{};
          auto built{nc::frame::classic(engine->id(), zeros)};
          for (nc::signal_entry const& entry : engine->signals()) {
            nu::discard(
              nc::encode(
                entry.definition,
                entry.plan,
                *built,
                entry.definition.name() == "engine_rpm" ? 800.0 + (beats % 50) * 100.0
                                                        : 80.0 + (beats % 40)
              )
            );
          }
          send(*built);
        }
      } else if (fd == bus->descriptor()) {
        while (true) {
          auto const received{bus->receive()};
          if (!received || !received->has_value()) {
            break;
          }
          ++rx_count;
          show_frame(**received, reg, last_value);
        }
      }
    }
  }

  std::println("console stopped (rx={} tx={})", rx_count, tx_count);
  return 0;
}

#else

#include <print>

auto main() -> int {
  std::println("console: requires Linux SocketCAN (epoll + a vcan/can interface)");
  return 0;
}

#endif
