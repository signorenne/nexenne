#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::filter module.
 *
 * Discrete-time digital filters for embedded signal conditioning, all sharing
 * the \c filter_like surface (\c push to feed one sample, \c value to read the
 * last output, \c reset to return to the initial state). Every filter is generic
 * over the sample type, holds its state inline (no heap, no allocation), and
 * processes one sample at a time, so it runs in an interrupt handler or a tight
 * control loop.
 *
 * The roster, by role:
 *
 * Linear smoothers:
 * - \c ema : exponential moving average (single-pole IIR).
 * - \c sma : simple moving average over a fixed window.
 * - \c lowpass : first-order IIR low-pass, cutoff in hertz.
 * - \c highpass : first-order IIR high-pass, cutoff in hertz.
 * - \c biquad : second-order IIR with low-pass, high-pass, band-pass, and
 *   notch design helpers.
 * - \c butterworth : maximally-flat second-order low-pass.
 * - \c fir : finite impulse response over a fixed tap set.
 *
 * Nonlinear and robust:
 * - \c median : fixed-window median, rejects isolated spikes.
 * - \c kalman : simplified one-dimensional Kalman filter.
 * - \c complementary : two-sensor fusion (for example gyro plus accelerometer).
 * - \c lms : least-mean-squares adaptive FIR filter (in adaptive.hpp).
 *
 * Control and shaping:
 * - \c slew : rate limiter, caps the change per sample.
 * - \c debounce : rejects bouncing on a discrete input by sample count.
 * - \c timed_debounce : the same, gated by an elapsed duration.
 * - \c hysteresis : two-threshold Schmitt trigger.
 * - \c glitch : rejects pulses shorter than a minimum width.
 *
 * Validation and guards:
 * - \c range_guard : clamps or rejects out-of-range samples.
 * - \c rate_guard : rejects samples that change faster than a limit.
 * - \c validator : passes samples through a caller predicate.
 * - \c majority : majority vote over a fixed window.
 * - \c stale_detector : flags a signal that has stopped updating.
 */

#include <nexenne/filter/adaptive.hpp>
#include <nexenne/filter/biquad.hpp>
#include <nexenne/filter/butterworth.hpp>
#include <nexenne/filter/complementary.hpp>
#include <nexenne/filter/concepts.hpp>
#include <nexenne/filter/debounce.hpp>
#include <nexenne/filter/ema.hpp>
#include <nexenne/filter/fir.hpp>
#include <nexenne/filter/glitch.hpp>
#include <nexenne/filter/highpass.hpp>
#include <nexenne/filter/hysteresis.hpp>
#include <nexenne/filter/kalman.hpp>
#include <nexenne/filter/lowpass.hpp>
#include <nexenne/filter/majority.hpp>
#include <nexenne/filter/median.hpp>
#include <nexenne/filter/range_guard.hpp>
#include <nexenne/filter/rate_guard.hpp>
#include <nexenne/filter/slew.hpp>
#include <nexenne/filter/sma.hpp>
#include <nexenne/filter/stale_detector.hpp>
#include <nexenne/filter/timed_debounce.hpp>
#include <nexenne/filter/validator.hpp>

namespace nexenne::filter {}
