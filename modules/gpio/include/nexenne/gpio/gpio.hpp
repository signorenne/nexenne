#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::gpio module.
 *
 * \c nexenne::gpio is a GPIO line abstraction that reads a button, drives a
 * LED, and receives an edge, once, on backends that do not resemble each
 * other: the Linux character device, an in-memory test double, or a
 * user-supplied type over an embedded HAL. The core is header-only,
 * heap-free, and exception-free, so the same application code runs on a
 * Linux host and a bare-metal target.
 *
 * The module is layered:
 *
 * 1. Vocabulary: the enums, the strong identifiers, the polarity mapping,
 *    and the event clock (line_types.hpp); the error policy (error.hpp).
 * 2. Description and observation: what a line is (line_spec.hpp), how one
 *    request wants it (line_config.hpp), what a backend emits
 *    (line_event.hpp), and what applications consume (line_value.hpp).
 * 3. Contracts: the backend concept ladder (backend.hpp) and the edge
 *    transport seam (sink.hpp); backends live under io/.
 * 4. Handles and edge path: polarity-applying handles (line.hpp, chip.hpp),
 *    the physical-to-logical decode (decode.hpp), the userspace debounce
 *    (debounce.hpp), and drop detection (sequence_tracker.hpp).
 *
 * The concrete backends and transports are under \c nexenne/gpio/io/ and
 * are pulled in here too: the platform-guarded ones (chardev_chip.hpp,
 * chardev_info.hpp) compile everywhere and report \c gpio_error::unsupported
 * off Linux. Formatting stays opt-in through format.hpp, which this header
 * deliberately does not include.
 */

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/chip.hpp>
#include <nexenne/gpio/debounce.hpp>
#include <nexenne/gpio/decode.hpp>
#include <nexenne/gpio/drain.hpp>
#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/io/callback_sink.hpp>
#include <nexenne/gpio/io/chardev_chip.hpp>
#include <nexenne/gpio/io/chardev_info.hpp>
#include <nexenne/gpio/io/mock_chip.hpp>
#include <nexenne/gpio/io/queue_sink.hpp>
#include <nexenne/gpio/line.hpp>
#include <nexenne/gpio/line_config.hpp>
#include <nexenne/gpio/line_event.hpp>
#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>
#include <nexenne/gpio/line_value.hpp>
#include <nexenne/gpio/sequence_tracker.hpp>
#include <nexenne/gpio/sink.hpp>

namespace nexenne::gpio {}
