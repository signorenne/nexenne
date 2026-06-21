#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::math module.
 *
 * Includes everything currently ported: the scalar layer (constants, scalar
 * utilities, powers, three-speed trigonometry), the strong angle and Q-format
 * fixed-point types, the fixed-size vectors (with their algorithms and the
 * unit-length wrapper), and the column-major matrices with the projection
 * builders. The rotation types (quaternion, Euler), geometric samplers, and the
 * hashing and formatting support join this list as they are ported.
 */

#include <nexenne/math/angle.hpp>
#include <nexenne/math/concepts.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/error.hpp>
#include <nexenne/math/fixed.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/projection.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/trigonometry.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>
