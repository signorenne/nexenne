#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::can module.
 *
 * Includes every public header of the CAN bus communications module: the error
 * policy, the wire frame layer, the signal codec, the message database, the bus
 * IO layer, and their formatters.
 */

#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/dlc.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>

namespace nexenne::can {}
