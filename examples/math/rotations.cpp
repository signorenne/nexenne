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

  // Compare the interpolation variants on the same endpoint pair. slerp walks the
  // arc at constant angular velocity, so its angle is exactly linear in t (a 90
  // degree arc gives 22.5, 45, 67.5 degrees at t = 0.25, 0.5, 0.75). nlerp_short
  // blends the four components and renormalizes: cheaper, but the angle is not
  // linear in t, so it lags slerp off the midpoint (the two coincide at t = 0.5 by
  // symmetry). nlerp_plain is the same blend with no shorter-arc fix; the endpoints
  // here already share a hemisphere, so it agrees with nlerp_short. See
  // slerp_variants.hpp for when each one is the right pick.
  if (qz) {
    auto const id{nm::quaternion_d::identity()};
    for (double t : {0.25, 0.5, 0.75}) {
      auto const via_slerp{nm::to_axis_angle(nm::slerp(id, *qz, t)).angle().value()};
      auto const via_nlerp_short{nm::to_axis_angle(nm::nlerp_short(id, *qz, t)).angle().value()};
      auto const via_nlerp_plain{nm::to_axis_angle(nm::nlerp_plain(id, *qz, t)).angle().value()};
      std::println(
        "t={:.2f} angle: slerp={:.4f} nlerp_short={:.4f} nlerp_plain={:.4f} rad",
        t,
        via_slerp,
        via_nlerp_short,
        via_nlerp_plain
      );
    }
  }

  // Euler angles (aerospace yaw-pitch-roll) to a quaternion and back to an axis.
  auto const aircraft{nm::from_ypr(nm::radians{0.4}, nm::radians{-0.2}, nm::radians{0.9})};
  auto const aa{nm::to_axis_angle(aircraft)};
  std::println("{:<28} {:.4f}", "from_ypr -> rotation axis", aa.axis());
  std::println("from_ypr -> angle = {:.4f} rad", aa.angle().value());
  return 0;
}
