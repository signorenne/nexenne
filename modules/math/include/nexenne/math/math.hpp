#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::math module.
 *
 * Includes everything currently ported: the scalar layer (constants, scalar
 * utilities, powers, three-speed trigonometry), the strong angle and Q-format
 * fixed-point types, the fixed-size vectors (with their algorithms and the
 * unit-length wrapper), the column-major matrices with the projection builders,
 * the rotation types (unit quaternions with slerp variants, Euler angles), the
 * affine transform builders and parametric curves, the geometric random
 * samplers, and the hashing and formatting support. Including this header pulls
 * in the whole module.
 */

#include <nexenne/math/angle.hpp>
#include <nexenne/math/concepts.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/curve.hpp>
#include <nexenne/math/error.hpp>
#include <nexenne/math/euler.hpp>
#include <nexenne/math/fixed.hpp>
#include <nexenne/math/format.hpp>
#include <nexenne/math/hash.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/normalized.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/projection.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/random.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/slerp_variants.hpp>
#include <nexenne/math/transform.hpp>
#include <nexenne/math/trigonometry.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>
