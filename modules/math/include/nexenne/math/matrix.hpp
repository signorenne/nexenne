#pragma once

/**
 * @file
 * @brief Square matrices stored column-major.
 *
 * The storage is \c std::array<vector<Value, N>, N>, one \c vector per column.
 * The element at row \c r and column \c c is \c m(r, c); whole columns are
 * \c m[c] or \c m.columns()[c]. Column-major matches OpenGL, Vulkan, and most
 * GPU APIs, so \c data() can be uploaded directly with no transpose, and it makes
 * the products vectorize (see below). The composition order for column vectors is
 * right to left: \c translate * rotate * scale * v applies scale, then rotation,
 * then translation.
 *
 * Why column-major helps SIMD. A matrix product over column-major storage is
 * naturally a *linear combination of the left matrix's columns*: result column
 * j is the sum over k of (a's column k) scaled by b(k, j). Each column is a
 * contiguous \c vector, so that sum is a chain of packed multiply-adds, and a
 * \c matrix<float,4> multiply compiles to packed SSE with no scalar fallback. The
 * naive row-times-column formulation would stride across columns to read a's
 * rows and defeat the vectorizer, so the products here are written the
 * column-combination way on purpose.
 *
 * Construction: there are no deduction guides (too many arguments to deduce
 * cleanly). Build via the aliases (\c matrix4_f, \c matrix3_d) with an array of
 * columns, or use the \c make_matrix2 / \c make_matrix3 / \c make_matrix4
 * row-major factories (you write the matrix in reading order, they store it
 * column-major). The projection builders (perspective, orthographic) are in
 * projection.hpp.
 */

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>

#include <nexenne/math/concepts.hpp>
#include <nexenne/math/error.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>

namespace nexenne::math {

/**
 * @brief Square \p N by \p N matrix of \p Value, stored column-major.
 *
 * @tparam Value Arithmetic component type.
 * @tparam N Dimension (number of rows and columns).
 */
template <arithmetic Value, std::size_t N>
class matrix {
public:
  using value_type = Value;                         ///< The scalar component type.
  using column_type = vector<Value, N>;             ///< The column vector type.
  using storage_type = std::array<column_type, N>;  ///< The column-array storage.

private:
  storage_type m_columns{};

public:
  /**
   * @brief Constructs a zero matrix.
   *
   * @pre None.
   * @post Every element is value-initialized to zero.
   */
  constexpr matrix() noexcept = default;

  /**
   * @brief Constructs from an array of column vectors.
   *
   * @param cols Column vectors, one per matrix column.
   *
   * @pre None.
   * @post Column \c c of the matrix equals \c cols[c].
   */
  explicit constexpr matrix(storage_type const cols) noexcept : m_columns{cols} {}

  /**
   * @brief Element access by (row, column).
   *
   * @param row Row index, less than \c N.
   * @param col Column index, less than \c N.
   *
   * @return Mutable reference to the element at \p row, \p col.
   *
   * @pre \p row and \p col are each less than \c N.
   * @post None.
   */
  [[nodiscard]] constexpr auto
  operator()(std::size_t const row, std::size_t const col) noexcept -> value_type& {
    return m_columns[col][row];
  }

  /**
   * @brief Element access by (row, column).
   *
   * @param row Row index, less than \c N.
   * @param col Column index, less than \c N.
   *
   * @return Const reference to the element at \p row, \p col.
   *
   * @pre \p row and \p col are each less than \c N.
   * @post None.
   */
  [[nodiscard]] constexpr auto
  operator()(std::size_t const row, std::size_t const col) const noexcept -> value_type const& {
    return m_columns[col][row];
  }

  /**
   * @brief Column access: \c m[c] is the c-th column as a vector.
   *
   * @param col Column index, less than \c N.
   *
   * @return Mutable reference to column \p col.
   *
   * @pre \p col is less than \c N.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](std::size_t const col) noexcept -> column_type& {
    return m_columns[col];
  }

  /**
   * @brief Column access: \c m[c] is the c-th column as a vector.
   *
   * @param col Column index, less than \c N.
   *
   * @return Const reference to column \p col.
   *
   * @pre \p col is less than \c N.
   * @post None.
   */
  [[nodiscard]] constexpr auto operator[](std::size_t const col
  ) const noexcept -> column_type const& {
    return m_columns[col];
  }

  /**
   * @brief Returns the r-th row as a value-copied vector.
   *
   * A row is strided across the column-major storage, so it is gathered into a
   * fresh vector rather than referenced.
   *
   * @param r Row index.
   *
   * @return A vector of the row's elements in column order.
   *
   * @pre \p r is less than \c N.
   * @post The returned vector holds the elements of row \p r.
   */
  [[nodiscard]] constexpr auto row(std::size_t const r) const noexcept -> column_type {
    auto result{column_type{}};
    for (std::size_t c{0}; c < N; ++c) {
      result[c] = m_columns[c][r];
    }
    return result;
  }

  /**
   * @brief Pointer to the first scalar of the contiguous column-major storage.
   *
   * @return Mutable pointer to \c N*N contiguous scalars, column by column.
   *
   * @pre None.
   * @post The returned pointer addresses \c N*N contiguous scalars.
   */
  [[nodiscard]] constexpr auto data() noexcept -> value_type* {
    return m_columns[0].data();
  }

  /**
   * @brief Pointer to the first scalar of the contiguous column-major storage.
   *
   * @return Const pointer to \c N*N contiguous scalars, column by column.
   *
   * @pre None.
   * @post The returned pointer addresses \c N*N contiguous scalars.
   */
  [[nodiscard]] constexpr auto data() const noexcept -> value_type const* {
    return m_columns[0].data();
  }

  /**
   * @brief Access to the underlying column storage.
   *
   * @return Const reference to the array of column vectors.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto columns() const noexcept -> storage_type const& {
    return m_columns;
  }

  /**
   * @brief Access to the underlying column storage.
   *
   * @return Mutable reference to the array of column vectors.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto columns() noexcept -> storage_type& {
    return m_columns;
  }

  /**
   * @brief Number of rows.
   *
   * @return The row count \c N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto rows() noexcept -> std::size_t {
    return N;
  }

  /**
   * @brief Number of columns.
   *
   * @return The column count \c N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto cols() noexcept -> std::size_t {
    return N;
  }

  /**
   * @brief Total element count.
   *
   * @return The element count \c N*N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto size() noexcept -> std::size_t {
    return N * N;
  }

  /**
   * @brief The identity matrix.
   *
   * @return The matrix with ones on the diagonal and zeros elsewhere.
   *
   * @pre None.
   * @post The result is the multiplicative identity for the dimension.
   */
  [[nodiscard]] static constexpr auto identity() noexcept -> matrix {
    auto m{matrix{}};
    for (std::size_t i{0}; i < N; ++i) {
      m.m_columns[i][i] = value_type{1};
    }
    return m;
  }

  /**
   * @brief The zero matrix.
   *
   * @return The matrix with every element zero.
   *
   * @pre None.
   * @post Every element of the result is zero.
   */
  [[nodiscard]] static constexpr auto zero() noexcept -> matrix {
    return matrix{};
  }

  /**
   * @brief A diagonal matrix with \p value on every diagonal entry.
   *
   * @param value Value placed on every diagonal entry.
   *
   * @return The diagonal matrix.
   *
   * @pre None.
   * @post Diagonal entries equal \p value; off-diagonal entries are zero.
   */
  [[nodiscard]] static constexpr auto diagonal(value_type const value) noexcept -> matrix {
    auto m{matrix{}};
    for (std::size_t i{0}; i < N; ++i) {
      m.m_columns[i][i] = value;
    }
    return m;
  }

  /**
   * @brief Lexicographically compares two matrices column by column.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return Three-way comparison result over the columns in order.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(matrix const& lhs, matrix const& rhs) noexcept = default;
};

/**
 * @brief Constructs a 2x2 matrix from four scalars in row-major (reading) order.
 *
 * You write the matrix as it reads on the page; it is stored column-major.
 *
 * @tparam Value Component type.
 * @param m00 Row 0, column 0.
 * @param m01 Row 0, column 1.
 * @param m10 Row 1, column 0.
 * @param m11 Row 1, column 1.
 *
 * @return The 2x2 matrix with the specified elements.
 *
 * @pre None.
 * @post Element (r, c) equals the argument named \c mRC.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto make_matrix2(
  Value const m00, Value const m01, Value const m10, Value const m11
) noexcept -> matrix<Value, 2> {
  return matrix<Value, 2>{{{
    vector<Value, 2>{m00, m10},  // column 0
    vector<Value, 2>{m01, m11},  // column 1
  }}};
}

/**
 * @brief Constructs a 3x3 matrix from nine scalars in row-major (reading) order.
 *
 * @tparam Value Component type.
 * @param m00 Row 0, column 0.
 * @param m01 Row 0, column 1.
 * @param m02 Row 0, column 2.
 * @param m10 Row 1, column 0.
 * @param m11 Row 1, column 1.
 * @param m12 Row 1, column 2.
 * @param m20 Row 2, column 0.
 * @param m21 Row 2, column 1.
 * @param m22 Row 2, column 2.
 *
 * @return The 3x3 matrix with the specified elements.
 *
 * @pre None.
 * @post Element (r, c) equals the argument named \c mRC.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto make_matrix3(
  Value const m00,
  Value const m01,
  Value const m02,
  Value const m10,
  Value const m11,
  Value const m12,
  Value const m20,
  Value const m21,
  Value const m22
) noexcept -> matrix<Value, 3> {
  return matrix<Value, 3>{{{
    vector<Value, 3>{m00, m10, m20},
    vector<Value, 3>{m01, m11, m21},
    vector<Value, 3>{m02, m12, m22},
  }}};
}

/**
 * @brief Constructs a 4x4 matrix from sixteen scalars in row-major (reading) order.
 *
 * @tparam Value Component type.
 * @param m00 Row 0, column 0.
 * @param m01 Row 0, column 1.
 * @param m02 Row 0, column 2.
 * @param m03 Row 0, column 3.
 * @param m10 Row 1, column 0.
 * @param m11 Row 1, column 1.
 * @param m12 Row 1, column 2.
 * @param m13 Row 1, column 3.
 * @param m20 Row 2, column 0.
 * @param m21 Row 2, column 1.
 * @param m22 Row 2, column 2.
 * @param m23 Row 2, column 3.
 * @param m30 Row 3, column 0.
 * @param m31 Row 3, column 1.
 * @param m32 Row 3, column 2.
 * @param m33 Row 3, column 3.
 *
 * @return The 4x4 matrix with the specified elements.
 *
 * @pre None.
 * @post Element (r, c) equals the argument named \c mRC.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto make_matrix4(
  Value const m00,
  Value const m01,
  Value const m02,
  Value const m03,
  Value const m10,
  Value const m11,
  Value const m12,
  Value const m13,
  Value const m20,
  Value const m21,
  Value const m22,
  Value const m23,
  Value const m30,
  Value const m31,
  Value const m32,
  Value const m33
) noexcept -> matrix<Value, 4> {
  return matrix<Value, 4>{{{
    vector<Value, 4>{m00, m10, m20, m30},
    vector<Value, 4>{m01, m11, m21, m31},
    vector<Value, 4>{m02, m12, m22, m32},
    vector<Value, 4>{m03, m13, m23, m33},
  }}};
}

/// @brief 2x2 matrix of arbitrary component type.
template <arithmetic Value>
using matrix2 = matrix<Value, 2>;
/// @brief 3x3 matrix of arbitrary component type.
template <arithmetic Value>
using matrix3 = matrix<Value, 3>;
/// @brief 4x4 matrix of arbitrary component type.
template <arithmetic Value>
using matrix4 = matrix<Value, 4>;

/// @brief 2x2 \c float matrix.
using matrix2_f = matrix2<float>;
/// @brief 3x3 \c float matrix.
using matrix3_f = matrix3<float>;
/// @brief 4x4 \c float matrix.
using matrix4_f = matrix4<float>;
/// @brief 2x2 \c double matrix.
using matrix2_d = matrix2<double>;
/// @brief 3x3 \c double matrix.
using matrix3_d = matrix3<double>;
/// @brief 4x4 \c double matrix.
using matrix4_d = matrix4<double>;

// The storage is one std::array of column vectors: contiguous, no padding, no
// vtable, so a matrix is standard-layout and trivially copyable and data() is a
// valid column-major upload pointer.
static_assert(std::is_standard_layout_v<matrix<float, 2>>);
static_assert(std::is_standard_layout_v<matrix<float, 3>>);
static_assert(std::is_standard_layout_v<matrix<float, 4>>);
static_assert(sizeof(matrix<float, 2>) == 4 * sizeof(float));
static_assert(sizeof(matrix<float, 3>) == 9 * sizeof(float));
static_assert(sizeof(matrix<float, 4>) == 16 * sizeof(float));
static_assert(std::is_trivially_copyable_v<matrix<float, 2>>);
static_assert(std::is_trivially_copyable_v<matrix<float, 3>>);
static_assert(std::is_trivially_copyable_v<matrix<float, 4>>);

/**
 * @brief Element-wise matrix addition.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return The matrix whose element (r, c) is \c a(r,c) + b(r,c).
 *
 * @pre None.
 * @post The result has the same dimension as the operands.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator+(matrix<Value, N> const& a, matrix<Value, N> const& b) noexcept -> matrix<Value, N> {
  auto result{matrix<Value, N>{}};
  for (std::size_t c{0}; c < N; ++c) {
    result[c] = a[c] + b[c];  // per-column vector add (packed)
  }
  return result;
}

/**
 * @brief Element-wise matrix subtraction.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param a Minuend.
 * @param b Subtrahend.
 *
 * @return The matrix whose element (r, c) is \c a(r,c) - b(r,c).
 *
 * @pre None.
 * @post The result has the same dimension as the operands.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator-(matrix<Value, N> const& a, matrix<Value, N> const& b) noexcept -> matrix<Value, N> {
  auto result{matrix<Value, N>{}};
  for (std::size_t c{0}; c < N; ++c) {
    result[c] = a[c] - b[c];
  }
  return result;
}

/**
 * @brief Scalar multiplication (matrix on the left).
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param m Matrix to scale.
 * @param scalar Factor applied to every element.
 *
 * @return The matrix whose element (r, c) is \c m(r,c) * scalar.
 *
 * @pre None.
 * @post The result has the same dimension as \p m.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator*(matrix<Value, N> const& m, Value const scalar) noexcept -> matrix<Value, N> {
  auto result{matrix<Value, N>{}};
  for (std::size_t c{0}; c < N; ++c) {
    result[c] = m[c] * scalar;
  }
  return result;
}

/**
 * @brief Scalar multiplication (scalar on the left).
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param scalar Factor applied to every element.
 * @param m Matrix to scale.
 *
 * @return The matrix whose element (r, c) is \c scalar * m(r,c).
 *
 * @pre None.
 * @post The result equals \c m * scalar.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator*(Value const scalar, matrix<Value, N> const& m) noexcept -> matrix<Value, N> {
  return m * scalar;
}

/**
 * @brief Element-wise in-place addition.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param a Matrix mutated in place (left operand and destination).
 * @param b Matrix added to \p a.
 *
 * @return Reference to \p a after the addition.
 *
 * @pre None.
 * @post \p a holds the element-wise sum of its prior value and \p b.
 */
template <arithmetic Value, std::size_t N>
constexpr auto
operator+=(matrix<Value, N>& a, matrix<Value, N> const& b) noexcept -> matrix<Value, N>& {
  for (std::size_t c{0}; c < N; ++c) {
    a[c] += b[c];
  }
  return a;
}

/**
 * @brief Element-wise in-place subtraction.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param a Matrix mutated in place (minuend and destination).
 * @param b Matrix subtracted from \p a.
 *
 * @return Reference to \p a after the subtraction.
 *
 * @pre None.
 * @post \p a holds the element-wise difference of its prior value and \p b.
 */
template <arithmetic Value, std::size_t N>
constexpr auto
operator-=(matrix<Value, N>& a, matrix<Value, N> const& b) noexcept -> matrix<Value, N>& {
  for (std::size_t c{0}; c < N; ++c) {
    a[c] -= b[c];
  }
  return a;
}

/**
 * @brief In-place scalar multiplication.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param m Matrix mutated in place.
 * @param scalar Factor applied to every element.
 *
 * @return Reference to \p m after scaling.
 *
 * @pre None.
 * @post Every element of \p m is multiplied by \p scalar.
 */
template <arithmetic Value, std::size_t N>
constexpr auto operator*=(matrix<Value, N>& m, Value const scalar) noexcept -> matrix<Value, N>& {
  for (std::size_t c{0}; c < N; ++c) {
    m[c] *= scalar;
  }
  return m;
}

/**
 * @brief Matrix-matrix multiplication.
 *
 * Computes \c (A*B)(i,j) = sum_k A(i,k)*B(k,j), but evaluated column by column:
 * result column j is the linear combination of A's columns weighted by column j
 * of B. Each step is a packed vector multiply-add, so for \c N = 2, 3, 4 the
 * whole product compiles to packed SIMD (no strided row gather). See the file
 * header for why this column-combination form is the SIMD-friendly one.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return The product matrix.
 *
 * @pre None.
 * @post Result element (i, j) is the dot product of row i of \p a and column j
 *       of \p b.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator*(matrix<Value, N> const& a, matrix<Value, N> const& b) noexcept -> matrix<Value, N> {
  auto result{matrix<Value, N>{}};
  for (std::size_t j{0}; j < N; ++j) {
    auto col{vector<Value, N>{}};
    for (std::size_t k{0}; k < N; ++k) {
      col += a[k] * b(k, j);  // A's column k, scaled by B(k, j); packed mul-add
    }
    result[j] = col;
  }
  return result;
}

/**
 * @brief In-place matrix multiplication, equivalent to \c a = a*b.
 *
 * Uses a temporary (via the binary operator) because the product reads all of
 * \p a while writing it.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param a Left operand mutated in place (destination).
 * @param b Right operand.
 *
 * @return Reference to \p a after the multiplication.
 *
 * @pre None.
 * @post \p a holds the matrix product of its prior value and \p b.
 */
template <arithmetic Value, std::size_t N>
constexpr auto
operator*=(matrix<Value, N>& a, matrix<Value, N> const& b) noexcept -> matrix<Value, N>& {
  a = a * b;
  return a;
}

/**
 * @brief Matrix-vector multiplication: \c M*v, transforming a column vector.
 *
 * Evaluated as the linear combination of M's columns weighted by the components
 * of \p v, which is the column-major-friendly form: each term is a packed vector
 * scale-and-add rather than a strided row gather.
 *
 * @tparam Value Component type.
 * @tparam N Matrix and vector dimension.
 * @param m Matrix.
 * @param v Column vector.
 *
 * @return The transformed vector.
 *
 * @pre None.
 * @post Result component i is the dot product of row i of \p m and \p v.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
operator*(matrix<Value, N> const& m, vector<Value, N> const& v) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t j{0}; j < N; ++j) {
    result += m[j] * v[j];  // column j scaled by v[j]; packed mul-add
  }
  return result;
}

/**
 * @brief Returns the transpose of \p m.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param m Input matrix.
 *
 * @return The matrix with rows and columns swapped.
 *
 * @pre None.
 * @post Result element (r, c) equals \c m(c, r).
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto transpose(matrix<Value, N> const& m) noexcept -> matrix<Value, N> {
  auto result{matrix<Value, N>{}};
  for (std::size_t r{0}; r < N; ++r) {
    for (std::size_t c{0}; c < N; ++c) {
      result(c, r) = m(r, c);
    }
  }
  return result;
}

/**
 * @brief Determinant of a square matrix (dimension 2, 3, or 4).
 *
 * Closed-form expansions, no pivoting. For 2x2 it is the familiar
 * \c ad - bc; for 3x3 the rule of Sarrus written as a cofactor expansion along
 * the first row; for 4x4 a cofactor expansion along the first row that
 * precomputes the six 2x2 minors of the bottom two rows (\c t00..t05) and reuses
 * them across the four 3x3 cofactors. Dimensions above 4 would need LU
 * decomposition and are excluded by the \c requires clause.
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension (must be 2, 3, or 4).
 * @param m Input matrix.
 *
 * @return The determinant.
 *
 * @pre \c N is 2, 3, or 4.
 * @post None.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto determinant(matrix<Value, N> const& m) noexcept -> Value
  requires(N >= 2 && N <= 4)
{
  if constexpr (N == 2) {
    return m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0);
  } else if constexpr (N == 3) {
    return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1))
           - m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0))
           + m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
  } else {
    // N == 4: Laplace-expand along the first row. Each first-row cofactor is a
    // 3x3 determinant of rows 1..3, which itself expands along row 1 into the six
    // 2x2 minors of rows 2 and 3 - and those six are shared across all four
    // cofactors, so compute them once. Each t names the column pair it covers
    // (the two columns NOT struck out): t00 = cols{2,3}, t01 = {1,3}, t02 = {1,2},
    // t03 = {0,3}, t04 = {0,2}, t05 = {0,1}. The alternating +-+- on the m(0,j)
    // terms below is the checkerboard cofactor sign.
    auto const t00{m(2, 2) * m(3, 3) - m(2, 3) * m(3, 2)};
    auto const t01{m(2, 1) * m(3, 3) - m(2, 3) * m(3, 1)};
    auto const t02{m(2, 1) * m(3, 2) - m(2, 2) * m(3, 1)};
    auto const t03{m(2, 0) * m(3, 3) - m(2, 3) * m(3, 0)};
    auto const t04{m(2, 0) * m(3, 2) - m(2, 2) * m(3, 0)};
    auto const t05{m(2, 0) * m(3, 1) - m(2, 1) * m(3, 0)};
    return m(0, 0) * (m(1, 1) * t00 - m(1, 2) * t01 + m(1, 3) * t02)
           - m(0, 1) * (m(1, 0) * t00 - m(1, 2) * t03 + m(1, 3) * t04)
           + m(0, 2) * (m(1, 0) * t01 - m(1, 1) * t03 + m(1, 3) * t05)
           - m(0, 3) * (m(1, 0) * t02 - m(1, 1) * t04 + m(1, 2) * t05);
  }
}

/**
 * @brief Returns the inverse of \p m, or an error when it is singular.
 *
 * Uses the adjugate-over-determinant method with closed-form cofactors for
 * dimension 2, 3, and 4 (the 4x4 path expands the cofactors over six 2x2 minors
 * of the top two rows, \c s0..s5, and six of the bottom two rows, \c c0..c5).
 * The matrix is treated as singular, and an error returned, when \c |det| falls
 * to or below a numerical-zero threshold of 1e-20.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Matrix dimension (2, 3, or 4).
 * @param m Input matrix.
 *
 * @return The inverse on success, or \c math_error::singular_matrix when the
 *         determinant is at or below the numerical-zero threshold.
 *
 * @pre Components are finite.
 * @post On success the returned \c inv satisfies \c m*inv == identity() within
 *       rounding.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto inverse(matrix<Real, N> const& m) noexcept -> result<matrix<Real, N>>
  requires(N >= 2 && N <= 4)
{
  auto const det{determinant(m)};
  // Reject both a (near-)singular and a non-finite determinant. The plain
  // comparison would let a NaN det slip through (every comparison with NaN is
  // false) and an infinite det (from overflow) divides to a zero matrix; either
  // would otherwise be returned as a bogus "success", so guard finiteness
  // explicitly. The threshold is absolute, not scale- or precision-relative: a
  // matrix scaled by s has its determinant scaled by s^N, so this detects a
  // genuinely tiny pivot, not conditioning - rescale ill-scaled inputs first.
  if (!isfinite(det) || abs(det) <= static_cast<Real>(1e-20)) {
    return std::unexpected{math_error::singular_matrix};
  }
  auto const inv_det{Real{1} / det};

  if constexpr (N == 2) {
    // Swap the diagonal, negate the off-diagonal, divide by the determinant.
    auto result{matrix<Real, 2>{}};
    result(0, 0) = m(1, 1) * inv_det;
    result(0, 1) = -m(0, 1) * inv_det;
    result(1, 0) = -m(1, 0) * inv_det;
    result(1, 1) = m(0, 0) * inv_det;
    return result;
  } else if constexpr (N == 3) {
    // Transposed cofactor matrix (the adjugate), scaled by 1/det.
    auto result{matrix<Real, 3>{}};
    result(0, 0) = (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) * inv_det;
    result(0, 1) = -(m(0, 1) * m(2, 2) - m(0, 2) * m(2, 1)) * inv_det;
    result(0, 2) = (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) * inv_det;
    result(1, 0) = -(m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) * inv_det;
    result(1, 1) = (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) * inv_det;
    result(1, 2) = -(m(0, 0) * m(1, 2) - m(0, 2) * m(1, 0)) * inv_det;
    result(2, 0) = (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0)) * inv_det;
    result(2, 1) = -(m(0, 0) * m(2, 1) - m(0, 1) * m(2, 0)) * inv_det;
    result(2, 2) = (m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0)) * inv_det;
    return result;
  } else {
    // N == 4: inverse = adjugate / det, where the adjugate is the transpose of the
    // cofactor matrix, so result(i, j) = cofactor(j, i) / det. This uses the
    // Laplace-expansion-by-complementary-minors identity (the same factorization
    // GLM ships): every 3x3 cofactor of a 4x4 splits into a 2x2 minor of the top
    // two rows times a 2x2 minor of the bottom two rows. There are only six
    // distinct 2x2 minors per row-pair, so name them once - s0..s5 from rows 0,1
    // and c0..c5 from rows 2,3 (sX and cX cover complementary column pairs) - and
    // every adjugate entry below is a signed sum of products of one s and one c.
    // The alternating signs are the cofactor checkerboard.
    // https://en.wikipedia.org/wiki/Laplace_expansion#Laplace_expansion_of_a_determinant_by_complementary_minors
    auto const s0{m(0, 0) * m(1, 1) - m(1, 0) * m(0, 1)};
    auto const s1{m(0, 0) * m(1, 2) - m(1, 0) * m(0, 2)};
    auto const s2{m(0, 0) * m(1, 3) - m(1, 0) * m(0, 3)};
    auto const s3{m(0, 1) * m(1, 2) - m(1, 1) * m(0, 2)};
    auto const s4{m(0, 1) * m(1, 3) - m(1, 1) * m(0, 3)};
    auto const s5{m(0, 2) * m(1, 3) - m(1, 2) * m(0, 3)};

    auto const c5{m(2, 2) * m(3, 3) - m(3, 2) * m(2, 3)};
    auto const c4{m(2, 1) * m(3, 3) - m(3, 1) * m(2, 3)};
    auto const c3{m(2, 1) * m(3, 2) - m(3, 1) * m(2, 2)};
    auto const c2{m(2, 0) * m(3, 3) - m(3, 0) * m(2, 3)};
    auto const c1{m(2, 0) * m(3, 2) - m(3, 0) * m(2, 2)};
    auto const c0{m(2, 0) * m(3, 1) - m(3, 0) * m(2, 1)};

    auto result{matrix<Real, 4>{}};
    result(0, 0) = (m(1, 1) * c5 - m(1, 2) * c4 + m(1, 3) * c3) * inv_det;
    result(0, 1) = (-m(0, 1) * c5 + m(0, 2) * c4 - m(0, 3) * c3) * inv_det;
    result(0, 2) = (m(3, 1) * s5 - m(3, 2) * s4 + m(3, 3) * s3) * inv_det;
    result(0, 3) = (-m(2, 1) * s5 + m(2, 2) * s4 - m(2, 3) * s3) * inv_det;

    result(1, 0) = (-m(1, 0) * c5 + m(1, 2) * c2 - m(1, 3) * c1) * inv_det;
    result(1, 1) = (m(0, 0) * c5 - m(0, 2) * c2 + m(0, 3) * c1) * inv_det;
    result(1, 2) = (-m(3, 0) * s5 + m(3, 2) * s2 - m(3, 3) * s1) * inv_det;
    result(1, 3) = (m(2, 0) * s5 - m(2, 2) * s2 + m(2, 3) * s1) * inv_det;

    result(2, 0) = (m(1, 0) * c4 - m(1, 1) * c2 + m(1, 3) * c0) * inv_det;
    result(2, 1) = (-m(0, 0) * c4 + m(0, 1) * c2 - m(0, 3) * c0) * inv_det;
    result(2, 2) = (m(3, 0) * s4 - m(3, 1) * s2 + m(3, 3) * s0) * inv_det;
    result(2, 3) = (-m(2, 0) * s4 + m(2, 1) * s2 - m(2, 3) * s0) * inv_det;

    result(3, 0) = (-m(1, 0) * c3 + m(1, 1) * c1 - m(1, 2) * c0) * inv_det;
    result(3, 1) = (m(0, 0) * c3 - m(0, 1) * c1 + m(0, 2) * c0) * inv_det;
    result(3, 2) = (-m(3, 0) * s3 + m(3, 1) * s1 - m(3, 2) * s0) * inv_det;
    result(3, 3) = (m(2, 0) * s3 - m(2, 1) * s1 + m(2, 2) * s0) * inv_det;

    return result;
  }
}

/**
 * @brief Sum of the diagonal elements (the trace).
 *
 * @tparam Value Component type.
 * @tparam N Matrix dimension.
 * @param m Input matrix.
 *
 * @return The sum of the diagonal elements.
 *
 * @pre None.
 * @post The returned value is the sum of \c m(i, i) over all i.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto trace(matrix<Value, N> const& m) noexcept -> Value {
  auto sum{Value{}};
  for (std::size_t i{0}; i < N; ++i) {
    sum += m(i, i);
  }
  return sum;
}

}  // namespace nexenne::math
