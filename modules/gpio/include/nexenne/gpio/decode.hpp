#pragma once

/**
 * @file
 * @brief The one place a physical edge event becomes a logical observation.
 *
 * Backends emit \c line_event.hpp with the raw physical level and the
 * physical edge direction. \c decode turns that into the \c line_value.hpp
 * application code consumes, applying the spec's polarity to the level AND
 * the edge together, so an active-low line never surfaces an inconsistent
 * pair (a logical \c true with a falling edge that never happened). Keeping
 * the conversion in a single free function, instead of scattering polarity
 * math across call sites, is what makes the physical/logical boundary
 * auditable: grep for \c decode and you have every crossing.
 */

#include <nexenne/gpio/line_event.hpp>
#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>
#include <nexenne/gpio/line_value.hpp>

namespace nexenne::gpio {

/**
 * @brief Decodes a physical edge event into a logical observation.
 *
 * Applies \p spec's polarity to the event's level and edge direction, takes
 * the name from \p spec, and carries the sequence number and timestamp
 * through unchanged. The identity (chip, offset) also comes from \p spec:
 * callers route each event to the spec it belongs to.
 *
 * @param spec Spec of the line the event belongs to; its name storage must
 *             outlive the returned value.
 * @param event Raw physical event from a backend.
 *
 * @return The polarity-corrected logical observation.
 *
 * @pre \p event addresses the line \p spec describes.
 * @post The result's \c logical() and \c edge() are polarity-applied and its
 *       \c sequence() and \c timestamp() mirror \p event.
 *
 * @complexity \c O(1).
 */
[[nodiscard]] constexpr auto decode(line_spec const& spec, line_event const& event) noexcept
  -> line_value {
  return line_value{
    spec,
    spec.to_logical(event.physical),
    apply_polarity(event.edge, spec.polarity()),
    event.sequence,
    event.timestamp,
  };
}

}  // namespace nexenne::gpio
