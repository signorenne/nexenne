/**
 * @file
 * @brief Affine transform builders: compose a model matrix, apply it, look at.
 *
 *   1. Compose a model matrix as translate * rotate about Y * scale; it applies
 *      right to left, so the scale acts first.
 *   2. A point picks up scale, rotation and translation; a direction, a free
 *      vector, ignores the translation.
 *   3. A view matrix maps the eye to the origin and puts the target on -Z.
 */

#include <print>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/format.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/transform.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  auto const rot{nm::from_axis_angle(nm::vector3_d{0, 1, 0}, nm::radians_d{nm::half_pi})};
  if (rot) {
    auto const model{
      nm::translation3(nm::vector3_d{0, 1, 0}) * nm::rotation3(*rot)
      * nm::scale3(nm::vector3_d{2, 2, 2})
    };

    std::println(
      "{:<32} {:.4f}", "model * point (1,0,0)", nm::transform_point(model, nm::vector3_d{1, 0, 0})
    );
    std::println(
      "{:<32} {:.4f}", "model * dir (1,0,0)", nm::transform_direction(model, nm::vector3_d{1, 0, 0})
    );
  }

  auto const view{
    nm::look_at(nm::vector3_d{0, 0, 5}, nm::vector3_d{0, 0, 0}, nm::vector3_d{0, 1, 0})
  };
  if (view) {
    std::println("{:<32} {:.4f}", "view * eye", nm::transform_point(*view, nm::vector3_d{0, 0, 5}));
    std::println(
      "{:<32} {:.4f}", "view * target", nm::transform_point(*view, nm::vector3_d{0, 0, 0})
    );
  }
  return 0;
}
