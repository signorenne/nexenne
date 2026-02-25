#pragma once

/**
 * @file
 * @brief Fast Fourier Transform (radix-2 Cooley-Tukey, in place).
 *
 * Converts a sequence between time and frequency domains in O(N log N) instead
 * of the naive DFT's O(N^2): the foundation of digital signal processing.
 * \c fft is the in-place complex-to-complex forward transform; \c ifft is its
 * inverse (dividing by N); \c rfft is the real-input forward transform, taking
 * N real samples and producing N/2 + 1 complex bins by exploiting the Hermitian
 * symmetry of real inputs.
 *
 * Input size must be a power of two; otherwise the routine returns
 * \c numerical_error::invalid_size (pad with zeros to the next power of two).
 * Zero-length input returns immediately. For \c fft / \c ifft, bin 0 is the DC
 * offset, bin k is frequency k*fs/N, and bin N/2 is Nyquist; for \c rfft only
 * bins \c [0, N/2] are stored, the rest being conjugates.
 */

#include <complex>
#include <concepts>
#include <cstddef>
#include <expected>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <nexenne/algorithm/numerical/numerical_error.hpp>

namespace nexenne::algorithm {

namespace detail {

template <std::floating_point T>
auto fft_inplace(std::span<std::complex<T>> const data, bool const inverse) noexcept -> void {
  auto const n{data.size()};
  if (n < 2) {
    return;
  }

  // Bit-reverse permutation (classical Gold-Rader form).
  for (auto i{std::size_t{1}}, j{std::size_t{0}}; i < n; ++i) {
    auto bit{n >> 1u};
    for (; (j & bit) != 0; bit >>= 1u) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(data[i], data[j]);
    }
  }

  // Iterative Cooley-Tukey butterflies; len doubles each stage, half is the
  // size of the lower butterfly arm.
  auto const sign{inverse ? T{1} : T{-1}};
  for (auto len{std::size_t{2}}; len <= n; len <<= 1u) {
    auto const half{len >> 1u};
    auto const angle{sign * T{2} * std::numbers::pi_v<T> / static_cast<T>(len)};
    auto const wn{std::complex<T>{std::cos(angle), std::sin(angle)}};
    for (auto i{std::size_t{0}}; i < n; i += len) {
      auto w{std::complex<T>{T{1}, T{0}}};
      for (auto k{std::size_t{0}}; k < half; ++k) {
        auto const u{data[i + k]};
        auto const v{data[i + k + half] * w};
        data[i + k] = u + v;
        data[i + k + half] = u - v;
        w *= wn;
      }
    }
  }

  if (inverse) {
    auto const scale{T{1} / static_cast<T>(n)};
    for (auto& x : data) {
      x *= scale;
    }
  }
}

[[nodiscard]] constexpr auto is_power_of_two(std::size_t const n) noexcept -> bool {
  return n != 0 && (n & (n - 1u)) == 0;
}

}  // namespace detail

/**
 * @brief In-place radix-2 forward FFT of a complex sequence.
 *
 * Transforms \p data from time to frequency domain via iterative Cooley-Tukey,
 * overwriting it with its spectrum. An empty span is a no-op.
 *
 * @tparam T Floating-point sample type.
 * @param data Sequence to transform in place.
 *
 * @return An empty success, or \c numerical_error::invalid_size when
 *         \p data.size() is non-empty and not a power of two.
 *
 * @pre \p data.size() is zero or a power of two.
 * @post On success \p data holds the forward spectrum; on failure it is
 *       unchanged.
 *
 * @complexity \c O(N log N) time and \c O(1) auxiliary space.
 */
template <std::floating_point T>
[[nodiscard]] auto fft(std::span<std::complex<T>> const data
) noexcept -> std::expected<void, numerical_error> {
  if (data.empty()) {
    return {};
  }
  if (!detail::is_power_of_two(data.size())) {
    return std::unexpected{numerical_error::invalid_size};
  }
  detail::fft_inplace<T>(data, false);
  return {};
}

/**
 * @brief In-place radix-2 inverse FFT of a complex spectrum.
 *
 * Transforms \p data back to the time domain and divides by \c N, so
 * \c ifft(fft(x)) recovers \c x to within rounding. An empty span is a no-op.
 *
 * @tparam T Floating-point sample type.
 * @param data Spectrum to transform in place.
 *
 * @return An empty success, or \c numerical_error::invalid_size when
 *         \p data.size() is non-empty and not a power of two.
 *
 * @pre \p data.size() is zero or a power of two.
 * @post On success \p data holds the inverse transform scaled by \c 1/N; on
 *       failure it is unchanged.
 *
 * @complexity \c O(N log N) time and \c O(1) auxiliary space.
 */
template <std::floating_point T>
[[nodiscard]] auto ifft(std::span<std::complex<T>> const data
) noexcept -> std::expected<void, numerical_error> {
  if (data.empty()) {
    return {};
  }
  if (!detail::is_power_of_two(data.size())) {
    return std::unexpected{numerical_error::invalid_size};
  }
  detail::fft_inplace<T>(data, true);
  return {};
}

/**
 * @brief Forward FFT of a real-valued sequence into a caller buffer.
 *
 * Packs the \c N real samples as \c N/2 complex samples, runs a half-size
 * complex FFT, and untangles the result via Hermitian symmetry, roughly halving
 * the work of a full complex FFT. The output holds bins \c [0, N/2] inclusive.
 *
 * @tparam T Floating-point sample type.
 * @param input Real input samples; length \c N.
 * @param output Destination buffer receiving the first \c N/2 + 1 bins.
 *
 * @return An empty success; \c numerical_error::invalid_size when \p input is
 *         non-empty but below length 2 or not a power of two, or when \p output
 *         is smaller than \c N/2 + 1.
 *
 * @pre \p input.size() is zero, or a power of two of at least 2 with
 *      \p output.size() at least \c input.size() / 2 + 1.
 * @post On success \p output[0..N/2] holds the non-redundant spectrum; on
 *       failure neither span is modified.
 *
 * @complexity \c O(N log N) time and \c O(N) auxiliary space.
 */
template <std::floating_point T>
[[nodiscard]] auto rfft(std::span<T const> const input, std::span<std::complex<T>> const output)
  -> std::expected<void, numerical_error> {
  using err = numerical_error;

  auto const n{input.size()};
  if (n == 0) {
    return {};
  }
  if (!detail::is_power_of_two(n) || n < 2) {
    return std::unexpected{err::invalid_size};
  }
  if (output.size() < n / 2 + 1) {
    return std::unexpected{err::invalid_size};
  }

  // Pack into a half-size complex working buffer and transform it.
  auto const half{n / 2};
  auto buf{std::vector<std::complex<T>>(half)};
  for (auto k{std::size_t{0}}; k < half; ++k) {
    buf[k] = std::complex<T>{input[2 * k], input[2 * k + 1]};
  }
  detail::fft_inplace<T>(std::span<std::complex<T>>{buf}, false);

  // Untangle. With Z the half-size FFT and w = exp(-2*pi*i/N):
  //   X[k] = 0.5*(Z[k] + conj(Z[N/2-k])) - 0.5i*w^k*(Z[k] - conj(Z[N/2-k])).
  output[0] = std::complex<T>{buf[0].real() + buf[0].imag(), T{0}};
  output[half] = std::complex<T>{buf[0].real() - buf[0].imag(), T{0}};

  auto const angle_step{-std::numbers::pi_v<T> / static_cast<T>(half)};
  for (auto k{std::size_t{1}}; k < half; ++k) {
    auto const a{buf[k]};
    auto const b{std::conj(buf[half - k])};
    auto const sum{(a + b) * T{0.5}};
    auto const diff{(a - b) * std::complex<T>{T{0}, T{-0.5}}};
    auto const angle{angle_step * static_cast<T>(k)};
    auto const tw{std::complex<T>{std::cos(angle), std::sin(angle)}};
    output[k] = sum + tw * diff;
  }
  return {};
}

/**
 * @brief Forward FFT of a real-valued sequence, returning a fresh vector.
 *
 * Allocates an \c N/2 + 1 element vector and forwards to the buffer overload.
 *
 * @tparam T Floating-point sample type.
 * @param input Real input samples; length \c N.
 *
 * @return A vector of \c N/2 + 1 spectral bins on success (empty for empty
 *         \p input), or \c numerical_error::invalid_size when \p input is
 *         non-empty but below length 2 or not a power of two.
 *
 * @pre \p input.size() is zero, or a power of two of at least 2.
 * @post On success the returned vector holds the non-redundant spectrum.
 *
 * @complexity \c O(N log N) time and \c O(N) space.
 */
template <std::floating_point T>
[[nodiscard]] auto rfft(std::span<T const> const input
) -> std::expected<std::vector<std::complex<T>>, numerical_error> {
  auto const n{input.size()};
  if (n == 0) {
    return std::vector<std::complex<T>>{};
  }
  auto out{std::vector<std::complex<T>>(n / 2 + 1)};
  auto const r{rfft<T>(input, std::span<std::complex<T>>{out})};
  if (!r.has_value()) {
    return std::unexpected{r.error()};
  }
  return out;
}

}  // namespace nexenne::algorithm
