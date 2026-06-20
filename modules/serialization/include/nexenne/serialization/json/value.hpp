#pragma once

/**
 * @file
 * @brief JSON value, a variant-based DOM node.
 *
 * Holds one of seven kinds in line with the JSON spec:
 *
 *   - null
 *   - boolean
 *   - integer (\c int64_t)
 *   - floating (\c double)
 *   - string
 *   - array
 *   - object (key-ordered via a sorted flat_map for deterministic
 *     serialisation)
 *
 * The integer / floating distinction goes beyond strict JSON
 * (which only has "number") but matches how most consumers
 * use the format, preserving \c int64_t round-trip semantics
 * for IDs, timestamps, counters, etc. The parser stores
 * something with a fractional part or exponent as floating;
 * everything else as integer.
 *
 * Construction: implicit from each primitive, so DOM building
 * looks like literal data:
 *
 * \code
 * using namespace nexenne::serialization::json;
 *
 * auto v{value{object{
 *     {"name",   "alice"},
 *     {"age",    30},
 *     {"admin",  true},
 *     {"scores", array{42, 73, 99}},
 * }}};
 * \endcode
 *
 * Access: \c operator[] returns references for mutation;
 * \c get<T>() returns \c std::optional for safe typed reads;
 * \c at_path() walks an RFC 6901 JSON Pointer for deep lookup.
 */

#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nexenne/container/flat_map.hpp>
#include <nexenne/serialization/error.hpp>

namespace nexenne::serialization::json {

class value;

/// @brief A JSON array, an ordered sequence of \c value nodes.
using array = std::vector<value>;

/**
 * @brief A JSON object, keys mapped to \c value nodes.
 *
 * Backed by \c nexenne::container::flat_map: a sorted contiguous vector of
 * pairs rather than a node-per-entry tree, so lookups and iteration are
 * cache-friendly and an object costs no per-key heap allocation, which suits
 * the small objects and memory-constrained targets this module serves. The
 * transparent \c std::less<> comparator lets lookups take a \c std::string_view
 * without constructing a key. Iteration is in sorted key order, giving
 * deterministic serialisation.
 */
using object = nexenne::container::flat_map<std::string, value, std::less<>>;

/**
 * @brief A variant-based JSON DOM node holding one of seven kinds.
 *
 * Stores null, boolean, integer (\c int64_t), floating (\c double),
 * string, array, or object. Integers and floats are kept distinct so
 * \c int64_t identifiers and counters round-trip exactly. Construction is
 * implicit from each primitive so DOM literals read like data, and access
 * comes in three flavours: \c operator[] for mutable references,
 * \c get<T>() for safe optional reads, and \c at_path for JSON Pointer
 * lookups.
 *
 * @note A default-constructed \c value is null.
 */
class value {
public:
  using null_type = std::monostate;
  using bool_type = bool;
  using int_type = std::int64_t;
  using float_type = double;
  using string_type = std::string;
  using array_type = array;
  using object_type = object;

  /**
   * @brief Discriminator for the seven JSON kinds a \c value can hold.
   *
   * The numeric values match the index of the corresponding alternative
   * in the internal variant, so \c type() is a direct cast.
   */
  enum class kind : std::uint8_t {
    null_kind = 0,      ///< Holds null (the default-constructed state).
    boolean_kind = 1,   ///< Holds a \c bool.
    integer_kind = 2,   ///< Holds a signed 64-bit integer.
    floating_kind = 3,  ///< Holds a \c double.
    string_kind = 4,    ///< Holds a UTF-8 string.
    array_kind = 5,     ///< Holds an ordered array of values.
    object_kind = 6,    ///< Holds a key-ordered object of values.
  };

private:
  using storage_type =
    std::variant<null_type, bool_type, int_type, float_type, string_type, array_type, object_type>;

  storage_type m_data{};

public:
  /**
   * @brief Construct a null value.
   *
   * @pre None.
   * @post \c is_null() is \c true.
   */
  value() noexcept = default;

  /**
   * @brief Destroys the value, tearing nested containers down iteratively.
   *
   * A deeply nested array or object (one built by hand past the parser's
   * \c max_depth bound) would overflow the stack under the default recursive
   * variant destructor. This flattens the teardown onto a heap worklist, so an
   * arbitrarily deep DOM is released without unbounded recursion.
   *
   * @pre None.
   * @post All owned storage is released.
   */
  ~value() {
    auto pending{std::vector<storage_type>{}};
    auto harvest{[&pending](storage_type& s) {
      if (auto* const a{std::get_if<array_type>(&s)}) {
        for (auto& child : *a) {
          pending.push_back(std::move(child.m_data));
        }
      } else if (auto* const o{std::get_if<object_type>(&s)}) {
        for (auto& entry : *o) {
          pending.push_back(std::move(entry.second.m_data));
        }
      }
    }};
    harvest(m_data);
    while (!pending.empty()) {
      auto node{std::move(pending.back())};
      pending.pop_back();
      harvest(node);  // node's children are moved out, so it destructs shallowly
    }
  }

  /**
   * @brief Copies a value (deep copy).
   *
   * @param other Value to copy.
   *
   * @pre None.
   * @post This value equals \p other.
   *
   * @note A deep copy recurses per nesting level, so copying an extremely deep
   *       DOM can overflow the stack; parsed values are bounded by
   *       \c max_depth, and a hand-built DOM should be kept shallow.
   */
  value(value const& other) = default;

  /**
   * @brief Moves a value, leaving the source in a valid empty state.
   *
   * @param other Value to move from.
   *
   * @pre None.
   * @post This value owns \p other's former contents.
   */
  value(value&& other) noexcept = default;

  /**
   * @brief Copy-assigns a value (deep copy).
   *
   * @param other Value to copy.
   *
   * @return A reference to this value.
   *
   * @pre None.
   * @post This value equals \p other.
   */
  auto operator=(value const& other) -> value& = default;

  /**
   * @brief Move-assigns a value.
   *
   * @param other Value to move from.
   *
   * @return A reference to this value.
   *
   * @pre None.
   * @post This value owns \p other's former contents.
   */
  auto operator=(value&& other) noexcept -> value& = default;

  /**
   * @brief Construct a null value from \c nullptr.
   *
   * Implicit so a literal \c nullptr can stand in for JSON null. Decays
   * to the same state as the default constructor.
   *
   * @pre None.
   * @post \c is_null() is \c true.
   */
  value(std::nullptr_t) noexcept  // NOLINT(google-explicit-constructor)
      : m_data{null_type{}} {}

  /**
   * @brief Construct a boolean value.
   *
   * @param b  The boolean to store.
   *
   * @pre None.
   * @post \c is_bool() is \c true and \c as_bool() yields \p b.
   */
  value(bool_type const b) noexcept  // NOLINT
      : m_data{b} {}

  /**
   * @brief Construct an integer value from any non-\c bool integral.
   *
   * The argument is widened to \c int_type. The \c bool overload is
   * excluded so \c true and \c false stay booleans.
   *
   * @tparam I  Integral type other than \c bool.
   * @param i  Integer to store, widened to \c int64_t.
   *
   * @pre None.
   * @post \c is_integer() is \c true.
   */
  template <std::integral I>
    requires(!std::same_as<I, bool>)
  value(I const i) noexcept  // NOLINT
      : m_data{static_cast<int_type>(i)} {}

  /**
   * @brief Construct a floating value from any floating-point type.
   *
   * The argument is widened to \c double.
   *
   * @tparam F  Floating-point type.
   * @param f  Value to store, widened to \c double.
   *
   * @pre None.
   * @post \c is_floating() is \c true.
   */
  template <std::floating_point F>
  value(F const f) noexcept  // NOLINT
      : m_data{static_cast<float_type>(f)} {}

  /**
   * @brief Construct a string value, taking ownership of \p s.
   *
   * @param s  String moved into the node.
   *
   * @pre None.
   * @post \c is_string() is \c true.
   */
  value(string_type s)  // NOLINT
      : m_data{std::move(s)} {}

  /**
   * @brief Construct a string value by copying from a view.
   *
   * @param sv  Characters copied into the node.
   *
   * @pre \p sv refers to valid characters for the duration of the call.
   * @post \c is_string() is \c true.
   */
  value(std::string_view const sv)  // NOLINT
      : m_data{string_type{sv}} {}

  /**
   * @brief Construct a string value from a null-terminated C string.
   *
   * @param s  Null-terminated string copied into the node.
   *
   * @pre \p s is non-null and null-terminated.
   * @post \c is_string() is \c true.
   */
  value(char const* const s)  // NOLINT
      : m_data{string_type{s}} {}

  /**
   * @brief Construct an array value, taking ownership of \p a.
   *
   * @param a  Array of child values moved into the node.
   *
   * @pre None.
   * @post \c is_array() is \c true.
   */
  value(array_type a)  // NOLINT
      : m_data{std::move(a)} {}

  /**
   * @brief Construct an object value, taking ownership of \p o.
   *
   * @param o  Key-ordered map of child values moved into the node.
   *
   * @pre None.
   * @post \c is_object() is \c true.
   */
  value(object_type o)  // NOLINT
      : m_data{std::move(o)} {}

  /**
   * @brief The kind currently held.
   *
   * @return The active \c kind discriminator.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto type() const noexcept -> kind {
    return static_cast<kind>(m_data.index());
  }

  /**
   * @brief Whether the value is null.
   *
   * @return \c true when the held kind is \c kind::null_kind.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_null() const noexcept -> bool {
    return type() == kind::null_kind;
  }

  /**
   * @brief Whether the value is a boolean.
   *
   * @return \c true when the held kind is \c kind::boolean_kind.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_bool() const noexcept -> bool {
    return type() == kind::boolean_kind;
  }

  /**
   * @brief Whether the value is a signed integer.
   *
   * @return \c true when the held kind is \c kind::integer_kind.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_integer() const noexcept -> bool {
    return type() == kind::integer_kind;
  }

  /**
   * @brief Whether the value is a floating-point number.
   *
   * @return \c true when the held kind is \c kind::floating_kind.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_floating() const noexcept -> bool {
    return type() == kind::floating_kind;
  }

  /**
   * @brief Whether the value is numeric (integer or floating).
   *
   * @return \c true when \c is_integer() or \c is_floating().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_number() const noexcept -> bool {
    return is_integer() || is_floating();
  }

  /**
   * @brief Whether the value is a string.
   *
   * @return \c true when the held kind is \c kind::string_kind.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_string() const noexcept -> bool {
    return type() == kind::string_kind;
  }

  /**
   * @brief Whether the value is an array.
   *
   * @return \c true when the held kind is \c kind::array_kind.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_array() const noexcept -> bool {
    return type() == kind::array_kind;
  }

  /**
   * @brief Whether the value is an object.
   *
   * @return \c true when the held kind is \c kind::object_kind.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_object() const noexcept -> bool {
    return type() == kind::object_kind;
  }

  /**
   * @brief Read the value as a boolean.
   *
   * @return The boolean on success.
   *
   * @pre None.
   * @post None.
   *
   * @throws None. Returns \c error::type_mismatch when the value is not
   *         a boolean.
   */
  [[nodiscard]] auto as_bool() const noexcept -> std::expected<bool_type, error> {
    if (auto const* p{std::get_if<bool_type>(&m_data)})
      return *p;
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Read the value as a signed integer.
   *
   * A floating value is accepted and truncated toward zero so callers
   * can read numbers uniformly.
   *
   * @return The integer on success.
   *
   * @pre None.
   * @post None.
   *
   * @throws None. Returns \c error::type_mismatch when the value is
   *         neither integer nor floating.
   */
  [[nodiscard]] auto as_int() const noexcept -> std::expected<int_type, error> {
    if (auto const* p{std::get_if<int_type>(&m_data)})
      return *p;
    if (auto const* p{std::get_if<float_type>(&m_data)}) {
      // Casting a non-finite or out-of-range double to an integer is undefined
      // behaviour. The upper bound rounds up to 2^63 (one past int64 max), so
      // reject with >=; the lower bound is exactly int64 min, so reject with <.
      constexpr auto lo{static_cast<float_type>(std::numeric_limits<int_type>::min())};
      constexpr auto hi{static_cast<float_type>(std::numeric_limits<int_type>::max())};
      if (!std::isfinite(*p) || *p < lo || *p >= hi) {
        return std::unexpected{error::type_mismatch};
      }
      return static_cast<int_type>(*p);
    }
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Read the value as a double.
   *
   * An integer value is accepted and widened to \c double.
   *
   * @return The floating value on success.
   *
   * @pre None.
   * @post None.
   *
   * @throws None. Returns \c error::type_mismatch when the value is
   *         neither floating nor integer.
   */
  [[nodiscard]] auto as_float() const noexcept -> std::expected<float_type, error> {
    if (auto const* p{std::get_if<float_type>(&m_data)})
      return *p;
    if (auto const* p{std::get_if<int_type>(&m_data)})
      return static_cast<float_type>(*p);
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Read the value as a string view.
   *
   * @return A view of the stored string on success.
   *
   * @pre None.
   * @post None.
   *
   * @throws None. Returns \c error::type_mismatch when the value is not
   *         a string.
   *
   * @warning The returned view is invalidated if this value is mutated
   *          or destroyed.
   */
  [[nodiscard]] auto as_string() const noexcept -> std::expected<std::string_view, error> {
    if (auto const* p{std::get_if<string_type>(&m_data)})
      return std::string_view{*p};
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Read the value as a reference to the underlying array.
   *
   * @return A \c const reference wrapper to the array on success.
   *
   * @pre None.
   * @post None.
   *
   * @throws None. Returns \c error::type_mismatch when the value is not
   *         an array.
   *
   * @warning The referenced array is invalidated if this value is
   *          mutated or destroyed.
   */
  [[nodiscard]] auto
  as_array() const noexcept -> std::expected<std::reference_wrapper<array_type const>, error> {
    if (auto const* p{std::get_if<array_type>(&m_data)})
      return std::cref(*p);
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Read the value as a reference to the underlying object.
   *
   * @return A \c const reference wrapper to the object on success.
   *
   * @pre None.
   * @post None.
   *
   * @throws None. Returns \c error::type_mismatch when the value is not
   *         an object.
   *
   * @warning The referenced object is invalidated if this value is
   *          mutated or destroyed.
   */
  [[nodiscard]] auto
  as_object() const noexcept -> std::expected<std::reference_wrapper<object_type const>, error> {
    if (auto const* p{std::get_if<object_type>(&m_data)})
      return std::cref(*p);
    return std::unexpected{error::type_mismatch};
  }

  /**
   * @brief Mutable reference to the underlying array.
   *
   * Unchecked: use only after confirming \c is_array().
   *
   * @return Mutable reference to the stored array.
   *
   * @pre \c is_array() is \c true.
   * @post None.
   *
   * @warning Behaviour is undefined (the underlying variant access
   *          throws \c std::bad_variant_access) when the value is not an
   *          array.
   */
  [[nodiscard]] auto array_mut() noexcept -> array_type& {
    return std::get<array_type>(m_data);
  }

  /**
   * @brief Mutable reference to the underlying object.
   *
   * Unchecked: use only after confirming \c is_object().
   *
   * @return Mutable reference to the stored object.
   *
   * @pre \c is_object() is \c true.
   * @post None.
   *
   * @warning Behaviour is undefined (the underlying variant access
   *          throws \c std::bad_variant_access) when the value is not an
   *          object.
   */
  [[nodiscard]] auto object_mut() noexcept -> object_type& {
    return std::get<object_type>(m_data);
  }

  /**
   * @brief Read the value as \p T, returning \c std::nullopt on mismatch.
   *
   * Dispatches to the matching typed accessor based on \p T: \c bool to
   * \c as_bool, any other integral to \c as_int, floating-point to
   * \c as_float, and \c std::string or \c std::string_view to
   * \c as_string. The numeric result is narrowed to \p T. Any other \p T
   * always yields \c std::nullopt.
   *
   * @tparam T  Target type to extract.
   *
   * @return The extracted value, or \c std::nullopt when the kind does
   *         not match \p T.
   *
   * @pre None.
   * @post None.
   *
   * @warning When \p T is \c std::string_view the returned view aliases
   *          this value and is invalidated by mutation or destruction.
   */
  template <typename T>
  [[nodiscard]] auto get() const noexcept -> std::optional<T> {
    if constexpr (std::same_as<T, bool>) {
      if (auto r{as_bool()}; r)
        return *r;
    } else if constexpr (std::integral<T>) {
      if (auto r{as_int()}; r)
        return static_cast<T>(*r);
    } else if constexpr (std::floating_point<T>) {
      if (auto r{as_float()}; r)
        return static_cast<T>(*r);
    } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
      if (auto r{as_string()}; r)
        return T{*r};
    }
    return std::nullopt;
  }

  /**
   * @brief Read the value as \p T, or return \p fallback on mismatch.
   *
   * Convenience wrapper over \c get that substitutes \p fallback when
   * the kind does not match \p T.
   *
   * @tparam T  Target type to extract.
   * @param fallback  Value returned when extraction fails.
   *
   * @return The extracted value, or \p fallback.
   *
   * @pre None.
   * @post None.
   */
  template <typename T>
  [[nodiscard]] auto get_or(T const& fallback) const noexcept -> T {
    return get<T>().value_or(fallback);
  }

  /**
   * @brief Mutable object-member access by key, inserting if absent.
   *
   * A null value is promoted to an empty object on first access, which
   * enables builder-style construction such as \c root["a"]["b"]=42. A
   * missing key is default-constructed (to null) and a reference to it
   * returned.
   *
   * @param key  Member name to look up or create.
   *
   * @return Mutable reference to the member value.
   *
   * @pre The value is null or already an object.
   * @post After the call the value is an object and \p key is present.
   *
   * @warning Behaviour is undefined (the underlying variant access
   *          throws \c std::bad_variant_access) when the value already
   *          holds a non-object, non-null kind.
   */
  [[nodiscard]] auto operator[](std::string_view const key) -> value& {
    if (is_null()) {
      m_data = object_type{};
    }
    auto& obj{std::get<object_type>(m_data)};
    auto it{obj.find(key)};  // transparent string_view lookup, no temporary key
    if (it == obj.end()) {
      it = obj.try_emplace(std::string{key}).first;
    }
    return it->second;
  }

  /**
   * @brief Read-only object-member access by key.
   *
   * Never mutates and never inserts. A missing key, or any non-object
   * value, yields a reference to a shared static null sentinel.
   *
   * @param key  Member name to look up.
   *
   * @return Reference to the member value, or to a null sentinel when
   *         absent.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto operator[](std::string_view const key) const noexcept -> value const& {
    static value const null_v{};
    if (auto const* obj{std::get_if<object_type>(&m_data)}) {
      auto const it{obj->find(key)};  // transparent string_view lookup, no temporary key
      if (it != obj->end())
        return it->second;
    }
    return null_v;
  }

  /**
   * @brief Mutable array-element access by index, unchecked.
   *
   * @param i  Zero-based element index.
   *
   * @return Mutable reference to the element at \p i.
   *
   * @pre The value is an array and \p i is less than its size.
   * @post None.
   *
   * @warning Behaviour is undefined when the value is not an array (the
   *          variant access throws \c std::bad_variant_access) or when
   *          \p i is out of range (the vector access is unchecked).
   */
  [[nodiscard]] auto operator[](std::size_t const i) noexcept -> value& {
    return std::get<array_type>(m_data)[i];
  }

  /**
   * @brief Read-only array-element access by index, bounds-checked.
   *
   * An out-of-range index, or any non-array value, yields a reference to
   * a shared static null sentinel.
   *
   * @param i  Zero-based element index.
   *
   * @return Reference to the element, or to a null sentinel when out of
   *         range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto operator[](std::size_t const i) const noexcept -> value const& {
    static value const null_v{};
    if (auto const* arr{std::get_if<array_type>(&m_data)}) {
      if (i < arr->size())
        return (*arr)[i];
    }
    return null_v;
  }

  /**
   * @brief Resolve an RFC 6901 JSON Pointer against this value.
   *
   * Walks slash-separated tokens, decoding the \c ~1 and \c ~0 escapes
   * for \c / and \c ~. Object tokens match member names; array tokens
   * must be all-digit indices. An empty path returns this value
   * unchanged.
   *
   * @param path  JSON Pointer, for example \c "/users/0/name".
   *
   * @return A reference wrapper to the resolved value on success.
   *
   * @pre None.
   * @post None.
   *
   * @throws None. Returns \c error::invalid_input when a non-empty path
   *         does not begin with \c '/', or \c error::path_not_found when
   *         any segment fails to resolve.
   *
   * @warning The returned reference aliases this value and is
   *          invalidated by mutation or destruction.
   */
  [[nodiscard]] auto at_path(std::string_view path
  ) const noexcept -> std::expected<std::reference_wrapper<value const>, error> {
    auto const* cur{this};
    if (path.empty()) {
      return std::cref(*cur);
    }
    if (path.front() != '/') {
      return std::unexpected{error::invalid_input};
    }
    path.remove_prefix(1);

    // path begins with '/', already stripped, so there is always at least one
    // reference token (possibly empty). Drive the loop per separator: a trailing
    // empty token must not be dropped (RFC 6901: "/" is the single token "", and
    // "/a/" descends into a's "" child).
    for (;;) {
      auto const slash{path.find('/')};
      auto const token{path.substr(0, slash)};

      auto decoded{std::string{}};
      decoded.reserve(token.size());
      for (std::size_t i{0}; i < token.size(); ++i) {
        if (token[i] == '~' && i + 1 < token.size()) {
          if (token[i + 1] == '0') {
            decoded.push_back('~');
            ++i;
          } else if (token[i + 1] == '1') {
            decoded.push_back('/');
            ++i;
          } else {
            decoded.push_back(token[i]);
          }
        } else {
          decoded.push_back(token[i]);
        }
      }

      if (cur->is_array()) {
        auto idx{std::size_t{0}};
        auto const* p{decoded.data()};
        auto const* e{decoded.data() + decoded.size()};
        if (p == e) {
          return std::unexpected{error::path_not_found};
        }
        // RFC 6901: an array index has no leading zeros (only "0" itself).
        if (decoded.size() > 1 && decoded[0] == '0') {
          return std::unexpected{error::path_not_found};
        }
        for (; p < e; ++p) {
          if (*p < '0' || *p > '9') {
            return std::unexpected{error::path_not_found};
          }
          auto const digit{static_cast<std::size_t>(*p - '0')};
          // Reject rather than let idx wrap (a wrapped index could resolve to
          // the wrong element instead of failing).
          if (idx > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return std::unexpected{error::path_not_found};
          }
          idx = idx * 10 + digit;
        }
        auto const& arr{std::get<array_type>(cur->m_data)};
        if (idx >= arr.size()) {
          return std::unexpected{error::path_not_found};
        }
        cur = &arr[idx];
      } else if (cur->is_object()) {
        auto const& obj{std::get<object_type>(cur->m_data)};
        auto const it{obj.find(decoded)};
        if (it == obj.end()) {
          return std::unexpected{error::path_not_found};
        }
        cur = &it->second;
      } else {
        return std::unexpected{error::path_not_found};
      }

      if (slash == std::string_view::npos) {
        break;
      }
      path.remove_prefix(slash + 1);
    }
    return std::cref(*cur);
  }

  /**
   * @brief Deep equality of two values.
   *
   * Compares the active kinds and their contents recursively. An integer
   * and a numerically equal floating value are distinct kinds and
   * therefore compare unequal.
   *
   * @param a  Left operand.
   * @param b  Right operand.
   *
   * @return \c true when both values hold the same kind and equal
   *         contents.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto operator==(value const& a, value const& b) noexcept -> bool {
    return a.m_data == b.m_data;
  }
};

}  // namespace nexenne::serialization::json
