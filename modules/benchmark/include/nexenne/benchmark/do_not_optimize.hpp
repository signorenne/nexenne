#pragma once

/**
 * @file
 * @brief Anti-dead-code-elimination primitives for micro-benchmarks.
 *
 * Compilers are aggressively allowed to optimise away computation whose result
 * is unused. In a benchmark loop that means your hot code may compile to
 * nothing, giving a meaninglessly fast measurement.
 *
 * \c do_not_optimize and \c clobber_memory are the standard inline-asm idioms
 * (popularised by Chandler Carruth's CppCon 2015 talk and used in
 * google/benchmark, nanobench, and Catch2) that mark values and memory as
 * "the optimiser must not assume this disappeared".
 *
 * \code
 *   auto v{compute_something()};
 *   nexenne::benchmark::do_not_optimize(v);  // pretend we use v
 *
 *   // ... mutate memory ...
 *   nexenne::benchmark::clobber_memory();    // pretend stores escape
 * \endcode
 *
 * Mechanism (GCC and Clang):
 *   - \c do_not_optimize(value): an empty asm block claims to read \c value via
 *     an any-operand constraint, so the compiler keeps \c value live and
 *     computed up to this point.
 *   - \c clobber_memory(): an empty asm block declares "memory" as a clobber,
 *     so the compiler flushes pending stores and may not reorder memory
 *     accesses across this point.
 *
 * On MSVC we fall back to volatile reads and \c _ReadWriteBarrier (deprecated
 * in newer MSVC but still effective here); the codegen is slightly worse than
 * the GCC and Clang variants but still defeats dead-code elimination.
 */

#if defined(_MSC_VER)
#  include <intrin.h>
#  pragma intrinsic(_ReadWriteBarrier)
#endif

// Force-inline marker: these primitives must never become a real function call,
// which would dwarf the benchmarked cost. Undefined at end of file so it does
// not leak to includers.
#if defined(__GNUC__) || defined(__clang__)
#  define NEXENNE_BENCHMARK_FORCE_INLINE [[gnu::always_inline]] inline
#elif defined(_MSC_VER)
#  define NEXENNE_BENCHMARK_FORCE_INLINE __forceinline
#else
#  define NEXENNE_BENCHMARK_FORCE_INLINE inline
#endif

namespace nexenne::benchmark {

#if defined(__GNUC__) || defined(__clang__)

/**
 * @brief Keeps \p value computed and alive at this point against DCE.
 *
 * GCC and Clang variant. An empty \c asm volatile block claims to read \p value
 * through an any-register-or-memory constraint, so the compiler must compute
 * and retain it even though it is apparently unused. Use on a benchmark's
 * result so the computation under test cannot be folded away.
 *
 * @tparam T Type of the value to protect.
 * @param value Read-only value to keep live.
 *
 * @pre None.
 * @post \p value is observably read at this program point and is unchanged.
 *
 * @warning Without a \c do_not_optimize on at least one output, a benchmark
 *          loop may compile to nothing and report a meaningless time.
 */
template <typename T>
NEXENNE_BENCHMARK_FORCE_INLINE auto do_not_optimize(T const& value) noexcept -> void {
  asm volatile("" : : "r,m"(value) : "memory");
}

/**
 * @brief Keeps a mutable \p value live across this point against DCE.
 *
 * GCC and Clang non-const lvalue overload. The empty \c asm volatile block
 * lists \p value as a read-write operand, so the compiler treats it as both
 * read and written, the correct semantics for in-place mutation inside a
 * benchmarked routine.
 *
 * @tparam T Type of the value to protect.
 * @param value Mutable value to keep live.
 *
 * @pre None.
 * @post \p value is observably read and written at this program point and its
 *       value is unchanged.
 */
template <typename T>
NEXENNE_BENCHMARK_FORCE_INLINE auto do_not_optimize(T& value) noexcept -> void {
#  if defined(__clang__)
  asm volatile("" : "+r,m"(value) : : "memory");
#  else
  asm volatile("" : "+m,r"(value) : : "memory");
#  endif
}

/**
 * @brief Acts as a compiler barrier so pending stores are not elided.
 *
 * GCC and Clang variant. An empty \c asm volatile block declares "memory" as a
 * clobber, telling the optimiser that some memory may have been modified at
 * this point. This prevents store-merging across the call and reordering of
 * subsequent reads.
 *
 * @pre None.
 * @post The compiler does not move memory accesses across this point; no memory
 *       is actually modified.
 */
NEXENNE_BENCHMARK_FORCE_INLINE auto clobber_memory() noexcept -> void {
  asm volatile("" : : : "memory");
}

#elif defined(_MSC_VER)

/**
 * @brief Keeps \p value computed and alive at this point against DCE.
 *
 * MSVC fallback. Reads \p value through a volatile reference and then issues a
 * \c _ReadWriteBarrier so the read cannot be elided. Codegen is slightly worse
 * than the GCC and Clang inline-asm form but still defeats DCE.
 *
 * @tparam T Type of the value to protect.
 * @param value Read-only value to keep live.
 *
 * @pre None.
 * @post \p value is observably read at this program point and is unchanged.
 */
template <typename T>
NEXENNE_BENCHMARK_FORCE_INLINE auto do_not_optimize(T const& value) noexcept -> void {
  auto const volatile& sink{value};
  static_cast<void>(sink);
  _ReadWriteBarrier();
}

/**
 * @brief Keeps a mutable \p value live across this point against DCE.
 *
 * MSVC non-const lvalue overload. Binds \p value to a volatile reference and
 * issues a \c _ReadWriteBarrier so neither the access nor surrounding stores
 * are reordered away.
 *
 * @tparam T Type of the value to protect.
 * @param value Mutable value to keep live.
 *
 * @pre None.
 * @post \p value is observably accessed at this program point and is unchanged.
 */
template <typename T>
NEXENNE_BENCHMARK_FORCE_INLINE auto do_not_optimize(T& value) noexcept -> void {
  auto volatile& sink{value};
  static_cast<void>(sink);
  _ReadWriteBarrier();
}

/**
 * @brief Acts as a compiler barrier so pending stores are not elided.
 *
 * MSVC fallback. Issues a \c _ReadWriteBarrier, which the compiler may not
 * reorder memory accesses across. Deprecated in newer MSVC but still effective
 * for this purpose.
 *
 * @pre None.
 * @post The compiler does not move memory accesses across this point; no memory
 *       is actually modified.
 */
NEXENNE_BENCHMARK_FORCE_INLINE auto clobber_memory() noexcept -> void {
  _ReadWriteBarrier();
}

#else

/**
 * @brief Keeps \p value computed and alive at this point against DCE.
 *
 * Best-effort fallback for unknown compilers. Reads \p value through a volatile
 * reference. Without an inline-asm or intrinsic barrier this is weaker than the
 * GCC, Clang, and MSVC forms, but the volatile read still discourages dead-code
 * elimination of the value.
 *
 * @tparam T Type of the value to protect.
 * @param value Read-only value to keep live.
 *
 * @pre None.
 * @post \p value is read at this program point and is unchanged.
 *
 * @warning On an unrecognised compiler this provides no memory barrier, so it
 *          may not fully defeat aggressive optimisation. Prefer a supported
 *          compiler for trustworthy benchmark numbers.
 */
template <typename T>
NEXENNE_BENCHMARK_FORCE_INLINE auto do_not_optimize(T const& value) noexcept -> void {
  auto const volatile& sink{value};
  static_cast<void>(sink);
}

/**
 * @brief Keeps a mutable \p value live across this point against DCE.
 *
 * Best-effort fallback for unknown compilers. Binds \p value to a volatile
 * reference. Weaker than the supported-compiler forms but still discourages
 * elimination of the value.
 *
 * @tparam T Type of the value to protect.
 * @param value Mutable value to keep live.
 *
 * @pre None.
 * @post \p value is accessed at this program point and is unchanged.
 *
 * @warning On an unrecognised compiler this provides no memory barrier.
 */
template <typename T>
NEXENNE_BENCHMARK_FORCE_INLINE auto do_not_optimize(T& value) noexcept -> void {
  auto volatile& sink{value};
  static_cast<void>(sink);
}

/**
 * @brief No-op compiler barrier for unknown compilers.
 *
 * Best-effort fallback. With no portable barrier available this does nothing,
 * so memory reordering cannot be prevented on an unrecognised compiler.
 *
 * @pre None.
 * @post No effect; no barrier is established.
 *
 * @warning On an unrecognised compiler this is a no-op and does not prevent
 *          store-merging or reordering.
 */
NEXENNE_BENCHMARK_FORCE_INLINE auto clobber_memory() noexcept -> void {}

#endif

}  // namespace nexenne::benchmark

#undef NEXENNE_BENCHMARK_FORCE_INLINE
