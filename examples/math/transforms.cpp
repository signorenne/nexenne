/**
 * @file
 * @brief Affine transform builders: compose a model matrix, apply it, look at.
 */

#include <print>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/transform.hpp>

namespace nm = nexenne::math;

namespace {

void print_vec(char const* label, nm::vector3_d const& v) {
  std::println("{:<32} ({:.4f}, {:.4f}, {:.4f})", label, v.x(), v.y(), v.z());
}

}  // namespace

auto main() -> int {
  // Compose a model matrix: scale, then rotate about Y, then translate
  // (applied right to left, so scale acts first).
  auto const rot{nm::from_axis_angle(nm::vector3_d{0, 1, 0}, nm::radians_d{nm::half_pi})};
  if (rot) {
    auto const model{
      nm::translation3(nm::vector3_d{0, 1, 0}) * nm::rotation3(*rot)
      * nm::scale3(nm::vector3_d{2, 2, 2})
    };

    // A point picks up the scale, rotation, and translation.
    print_vec("model * point (1,0,0)", nm::transform_point(model, nm::vector3_d{1, 0, 0}));
    // A direction ignores the translation (free vector).
    print_vec("model * dir (1,0,0)", nm::transform_direction(model, nm::vector3_d{1, 0, 0}));
  }

  // A view matrix: the eye maps to the origin, the target sits on -Z.
  auto const view{
    nm::look_at(nm::vector3_d{0, 0, 5}, nm::vector3_d{0, 0, 0}, nm::vector3_d{0, 1, 0})
  };
  if (view) {
    print_vec("view * eye", nm::transform_point(*view, nm::vector3_d{0, 0, 5}));
    print_vec("view * target", nm::transform_point(*view, nm::vector3_d{0, 0, 0}));
  }
  return 0;
}
