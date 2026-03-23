/**
 * @file
 * @brief Quaternions: build, rotate, compose, interpolate, and Euler angles.
 */

#include <print>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/euler.hpp>
#include <nexenne/math/format.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/slerp_variants.hpp>

namespace nm = nexenne::math;

auto main() -> int {
  // A 90-degree rotation about +Z takes +X to +Y.
  auto const qz{nm::from_axis_angle(nm::vector3_d{0, 0, 1}, nm::radians_d{nm::half_pi})};
  if (qz) {
    std::println(
      "{:<28} {:.4f}", "rotate (1,0,0) by 90 deg Z", nm::rotate(*qz, nm::vector3_d{1, 0, 0})
    );
  }

  // Compose: 90 about Z, then 90 about X (right to left).
  auto const qx{nm::from_axis_angle(nm::vector3_d{1, 0, 0}, nm::radians_d{nm::half_pi})};
  if (qz && qx) {
    auto const composed{*qx * *qz};
    std::println(
      "{:<28} {:.4f}", "compose (Zx then Xx)", nm::rotate(composed, nm::vector3_d{1, 0, 0})
    );
  }

  // Interpolate from identity to the Z rotation.
  if (qz) {
    auto const id{nm::quaternion_d::identity()};
    for (double t : {0.0, 0.5, 1.0}) {
      auto const s{nm::slerp(id, *qz, t)};
      auto const aa{nm::to_axis_angle(s)};
      std::println("slerp t={:.1f}: angle = {:.4f} rad", t, aa.angle().value());
    }
  }

  // Euler angles (aerospace yaw-pitch-roll) to a quaternion and back to an axis.
  auto const aircraft{nm::from_ypr(0.4, -0.2, 0.9)};
  auto const aa{nm::to_axis_angle(aircraft)};
  std::println("{:<28} {:.4f}", "from_ypr -> rotation axis", aa.axis());
  std::println("from_ypr -> angle = {:.4f} rad", aa.angle().value());
  return 0;
}
