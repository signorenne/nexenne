/**
 * @file
 * @brief Tests for the bus backend options.
 *
 * The defaults are pinned here because they are a contract with the kernel: a
 * bus opened with default options must behave the way a plain CAN_RAW socket
 * does.
 */

#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string>

#include <nexenne/can/format.hpp>
#include <nexenne/can/socket_options.hpp>

namespace {

namespace nc = nexenne::can;

TEST_CASE("socket_options: the defaults match a plain CAN_RAW socket") {
  nc::socket_options const options;
  CHECK_FALSE(options.fd_enabled);
  // CAN_RAW_RECV_OWN_MSGS is off in the kernel, so a caller that never asks for
  // its own sends does not read them back interleaved with real bus traffic.
  CHECK_FALSE(options.receive_own_messages);
  CHECK(options.nonblocking);
  CHECK(options.read_timeout_ms == 0);
}

TEST_CASE("socket_options: equality compares every option") {
  nc::socket_options const base;
  CHECK(base == nc::socket_options{});

  nc::socket_options fd{base};
  fd.fd_enabled = true;
  CHECK(fd != base);

  nc::socket_options own{base};
  own.receive_own_messages = true;
  CHECK(own != base);

  nc::socket_options blocking{base};
  blocking.nonblocking = false;
  CHECK(blocking != base);

  nc::socket_options timeout{base};
  timeout.read_timeout_ms = 250;
  CHECK(timeout != base);
}

TEST_CASE("socket_options: to_string reports every option") {
  CHECK(
    nc::to_string(nc::socket_options{})
    == "socket_options(fd=0, recv_own=0, nonblocking=1, timeout=0ms)"
  );

  nc::socket_options options;
  options.fd_enabled = true;
  options.receive_own_messages = true;
  options.nonblocking = false;
  options.read_timeout_ms = 250;
  CHECK(
    nc::to_string(options) == "socket_options(fd=1, recv_own=1, nonblocking=0, timeout=250ms)"
  );
}

TEST_CASE("socket_options: streaming and formatting agree with to_string") {
  nc::socket_options options;
  options.read_timeout_ms = 10;

  std::ostringstream stream;
  stream << options;
  CHECK(stream.str() == nc::to_string(options));
  CHECK(std::format("{}", options) == nc::to_string(options));
}

}  // namespace
