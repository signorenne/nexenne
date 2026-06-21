/**
 * @file
 * @brief Parametric curves: sample a Bezier, a Catmull-Rom path, and easing.
 */

#include <print>

#include <nexenne/math/curve.hpp>
#include <nexenne/math/vector.hpp>

namespace nm = nexenne::math;

namespace {

void print_pt(char const* label, nm::vector2_d const& p) {
  std::println("{:<26} ({:.4f}, {:.4f})", label, p.x(), p.y());
}

}  // namespace

auto main() -> int {
  // A cubic Bezier arch; sample position and tangent at the midpoint.
  nm::vector2_d const b0{0, 0};
  nm::vector2_d const b1{0, 1};
  nm::vector2_d const b2{1, 1};
  nm::vector2_d const b3{1, 0};
  print_pt("bezier_cubic t=0.5", nm::bezier_cubic(b0, b1, b2, b3, 0.5));
  print_pt("bezier tangent t=0.5", nm::bezier_cubic_tangent(b0, b1, b2, b3, 0.5));

  // A Catmull-Rom segment passes through its two inner control points.
  nm::vector2_d const c0{-1, 0};
  nm::vector2_d const c1{0, 0};
  nm::vector2_d const c2{1, 1};
  nm::vector2_d const c3{2, 1};
  for (double t : {0.0, 0.5, 1.0}) {
    print_pt("catmull_rom", nm::catmull_rom(c0, c1, c2, c3, t));
  }

  // Easing a single scalar (affine_point also admits a bare number).
  for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    std::println("ease_smootherstep({:.2f}) = {:.4f}", t, nm::ease_smootherstep(t));
  }
  return 0;
}
