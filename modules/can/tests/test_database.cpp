/**
 * @file
 * @brief Tests for building messages and an immutable database.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <cstddef>
#include <format>
#include <span>
#include <sstream>
#include <utility>

#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>

namespace {

namespace nc = nexenne::can;

auto build_db() -> nc::database {
  auto const status{nc::message_builder{nc::can_id::standard(0x100), "status"}
                      .add(
                        nc::signal_builder{}
                          .name("speed")
                          .start_bit(0)
                          .length(16)
                          .endianness(nc::byte_order::little_endian)
                          .scale(0.01)
                          .build()
                      )
                      .add(
                        nc::signal_builder{}
                          .name("oil_temp")
                          .start_bit(16)
                          .length(8)
                          .endianness(nc::byte_order::little_endian)
                          .scale(1.0)
                          .offset(-40.0)
                          .build()
                      )
                      .build()};
  auto const engine{nc::message_builder{nc::can_id::extended(0x18FEF100), "engine"}
                      .add(
                        nc::signal_builder{}
                          .name("rpm")
                          .start_bit(0)
                          .length(8)
                          .endianness(nc::byte_order::little_endian)
                          .build()
                      )
                      .build()};
  return nc::database_builder{}.add_message(status).add_message(engine).build();
}

TEST_CASE("message: collects signals and looks them up by name") {
  nc::message m{nc::can_id::standard(0x100), "status"};
  m.add_signal(
    nc::signal_builder{}
      .name("speed")
      .start_bit(0)
      .length(16)
      .endianness(nc::byte_order::little_endian)
      .build()
  );
  m.add_signal(
    nc::signal_builder{}
      .name("temp")
      .start_bit(16)
      .length(8)
      .endianness(nc::byte_order::little_endian)
      .build()
  );

  CHECK(m.signal_count() == 2);
  CHECK(m.name() == "status");
  REQUIRE(m.find_signal("temp").has_value());
  CHECK(*m.find_signal("temp") == 1);
  CHECK_FALSE(m.find_signal("missing").has_value());
}

TEST_CASE("message: a signal entry carries its compiled plan") {
  nc::message m{nc::can_id::standard(0x1), "m"};
  m.add_signal(nc::signal{0, 16, nc::byte_order::little_endian, false});
  CHECK(m.signals()[0].plan.bit_length() == 16);
  CHECK(m.signals()[0].plan.required_length() == 2);
}

TEST_CASE("database: holds the built messages in order") {
  auto const db{build_db()};
  CHECK(db.message_count() == 2);
  CHECK(db.messages()[0].name() == "status");
  CHECK(db.messages()[1].name() == "engine");
}

TEST_CASE("database: linear find matches identifier and format") {
  auto const db{build_db()};
  auto const* const status{db.find(nc::can_id::standard(0x100))};
  REQUIRE(status != nullptr);
  CHECK(status->name() == "status");

  auto const* const engine{db.find(nc::can_id::extended(0x18FEF100))};
  REQUIRE(engine != nullptr);
  CHECK(engine->name() == "engine");

  // A standard id with the same value as the extended message does not match.
  CHECK(db.find(nc::can_id::standard(0x100 & 0x7FF)) != engine);
  CHECK(db.find(nc::can_id::standard(0x200)) == nullptr);
}

TEST_CASE("database: a default database is empty") {
  nc::database const db;
  CHECK(db.message_count() == 0);
  CHECK(db.find(nc::can_id::standard(0x1)) == nullptr);
}

static_assert(
  std::same_as<decltype(std::declval<nc::message&>().signals()), std::span<nc::signal_entry const>>
);

TEST_CASE("signal_builder: formats the signal it would build") {
  auto const builder{nc::signal_builder{}
                       .name("speed")
                       .start_bit(0)
                       .length(16)
                       .endianness(nc::byte_order::little_endian)
                       .scale(0.01)
                       .unit("km/h")};
  CHECK(
    std::format("{}", builder)
    == "signal_builder(signal(speed @0:16 little_endian unsigned *0.01+0 km/h))"
  );

  std::ostringstream os{};
  os << builder;
  CHECK(os.str() == nc::to_string(builder));
}

TEST_CASE("message_builder: formats the message it would build") {
  auto builder{nc::message_builder{nc::can_id::standard(0x100), "status"}};
  builder.add(nc::signal_builder{}.name("speed").length(16).build());
  CHECK(
    std::format("{}", builder) == "message_builder(message(status @0x100 std, 8 bytes, 1 signals))"
  );

  std::ostringstream os{};
  os << builder;
  CHECK(os.str() == nc::to_string(builder));
}

TEST_CASE("database_builder: exposes and formats the messages added so far") {
  nc::database_builder builder;
  CHECK(builder.messages().empty());
  CHECK(std::format("{}", builder) == "database_builder(0 messages: [])");

  builder.add_message(nc::message_builder{nc::can_id::standard(0x100), "status"}.build());
  builder.add_message(nc::message_builder{nc::can_id::extended(0x18FEF100), "engine"}.build());
  REQUIRE(builder.messages().size() == 2);
  CHECK(builder.messages()[0].name() == "status");
  CHECK(builder.messages()[1].name() == "engine");
  CHECK(
    std::format("{}", builder)
    == "database_builder(2 messages: [message(status @0x100 std, 8 bytes, 0 signals), "
       "message(engine @0x18FEF100 ext, 8 bytes, 0 signals)])"
  );

  std::ostringstream os{};
  os << builder;
  CHECK(os.str() == nc::to_string(builder));

  auto const db{builder.build()};
  CHECK(db.message_count() == 2);
  CHECK(builder.messages().empty());
}

}  // namespace
