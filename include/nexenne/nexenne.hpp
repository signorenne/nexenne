#pragma once

/**
 * @file
 * @brief Umbrella header for the whole nexenne library.
 *
 * Includes every module enabled at build time. For finer-grained build
 * dependencies, prefer a single module umbrella header, for example
 * \c nexenne/math/math.hpp, or a leaf header. Add one guarded include here
 * as each module is ported into the tree.
 */

#if __has_include(<nexenne/utility/utility.hpp>)
#  include <nexenne/utility/utility.hpp>
#endif

namespace nexenne {}
