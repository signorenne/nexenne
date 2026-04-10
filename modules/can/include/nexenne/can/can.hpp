#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::can module.
 *
 * Includes every public header of the CAN bus communications module: the error
 * policy, the wire frame layer, the signal codec, the message database, the bus
 * IO layer, and their formatters.
 */

#include <nexenne/can/dlc.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>

namespace nexenne::can {}
