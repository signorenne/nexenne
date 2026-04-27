#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::can module.
 *
 * Includes every public header of the CAN bus communications module: the error
 * policy, the wire frame layer, the signal codec, the message database, the bus
 * IO layer, and their formatters.
 */

#include <nexenne/can/bus.hpp>
#include <nexenne/can/byte_field.hpp>
#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/codec.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/dlc.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/error_frame.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/loopback_bus.hpp>
#include <nexenne/can/io/socketcan_bus.hpp>
#include <nexenne/can/j1939_id.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/registry.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>
#include <nexenne/can/socket_options.hpp>

namespace nexenne::can {}
