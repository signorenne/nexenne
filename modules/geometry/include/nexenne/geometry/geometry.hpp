#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::geometry module.
 *
 * Pulls in every public header the module exposes. For finer-grained build
 * dependencies, include the individual leaf headers under
 * \c nexenne/geometry/ directly.
 */

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/circle.hpp>
#include <nexenne/geometry/error.hpp>
#include <nexenne/geometry/plane.hpp>
#include <nexenne/geometry/ray.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/triangle.hpp>

namespace nexenne::geometry {}
