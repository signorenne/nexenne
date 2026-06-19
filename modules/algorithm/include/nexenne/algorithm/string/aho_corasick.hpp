#pragma once

/**
 * @file
 * @brief Aho-Corasick multi-pattern matcher.
 *
 * Builds a trie over a set of patterns plus failure links, so one linear scan
 * over the text finds every occurrence of every pattern at once, in
 * \c O(|text| + sum(|patterns|) + matches) regardless of how many patterns are
 * in the set. Use it to search for many patterns simultaneously (spam filters,
 * blocklists, log scanning, dictionary lookup) over a fixed pattern set: build
 * once, scan many times.
 *
 * Each trie node owns a dense 256-entry transition array for branchless lookup,
 * so memory is about 1 KiB per node on 32-bit indices; a few hundred short
 * patterns typically stay under a few hundred KiB. Swap the array for a hash
 * map if a sparser representation is needed.
 *
 * @par Example
 * @code
 * auto m{aho_corasick{}};
 * m.add_pattern("alpha");
 * m.add_pattern("beta");
 * m.build();
 * m.scan(text, [](std::size_t pattern_id, std::size_t end_pos) {
 *   // pattern_id is the insertion order; end_pos is just past the match.
 * });
 * @endcode
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <string_view>
#include <type_traits>
#include <vector>

namespace nexenne::algorithm {

class aho_corasick {
public:
  /// @brief Index type identifying a trie node.
  using node_id = std::uint32_t;

  static constexpr node_id no_node{0xFFFFFFFFu};  ///< Sentinel for an absent transition.
  static constexpr node_id root_id{0};            ///< Identifier of the trie root node.

private:
  struct node {
    std::array<node_id, 256> next{};
    node_id fail{root_id};
    std::vector<std::size_t> terminals{};

    node() noexcept {
      next.fill(no_node);
    }
  };

  std::vector<node> m_nodes{};
  std::vector<std::size_t> m_patterns{};  // pattern id to length
  bool m_built{false};

public:
  /**
   * @brief Constructs an empty matcher holding only the root node.
   *
   * @pre None.
   * @post The trie holds the root node only, contains no patterns, and is not
   *       yet built.
   */
  aho_corasick() {
    m_nodes.emplace_back();  // the root
  }

  /**
   * @brief Inserts \p pattern into the matcher and returns its identifier.
   *
   * Walks or extends the trie one byte at a time and marks the terminal node.
   * Identifiers are assigned sequentially from 0 in insertion order.
   *
   * @param pattern Pattern to add.
   *
   * @return The zero-based identifier of the inserted pattern.
   *
   * @pre \c build() has not yet been called, and \p pattern is non-empty (an
   *      empty pattern has no well-defined match position in a substring
   *      matcher and is not supported).
   * @post The pattern is present in the trie, \c pattern_count() grows by one,
   *       and the returned identifier indexes the new pattern.
   *
   * @complexity \c O(|pattern|) amortised.
   */
  auto add_pattern(std::string_view const pattern) -> std::size_t {
    auto cur{root_id};
    for (auto const c : pattern) {
      auto const byte{static_cast<std::uint8_t>(c)};
      if (m_nodes[cur].next[byte] == no_node) {
        // emplace_back may reallocate, so hold no reference into m_nodes across
        // it: compute the new id, grow, then wire the edge afresh.
        auto const child{static_cast<node_id>(m_nodes.size())};
        m_nodes.emplace_back();
        m_nodes[cur].next[byte] = child;
      }
      cur = m_nodes[cur].next[byte];
    }
    auto const id{m_patterns.size()};
    m_patterns.push_back(pattern.size());
    m_nodes[cur].terminals.push_back(id);
    return id;
  }

  /**
   * @brief Computes failure links and goto transitions over the trie.
   *
   * A breadth-first pass wires each node's failure link, collapses missing
   * transitions into a complete automaton, and propagates dictionary-suffix
   * matches. Call after all patterns are added and before any scan.
   *
   * @pre All patterns intended for this matcher have been added.
   * @post The matcher is built and ready for \c scan; adding patterns
   *       afterwards is not supported.
   *
   * @complexity \c O(A * sum(|patterns|)) time, where \c A is the alphabet size
   *             of 256.
   */
  auto build() -> void {
    auto bfs{std::queue<node_id>{}};
    for (auto c{std::size_t{0}}; c < 256; ++c) {
      auto& next{m_nodes[root_id].next[c]};
      if (next == no_node) {
        next = root_id;
      } else {
        m_nodes[next].fail = root_id;
        bfs.push(next);
      }
    }
    while (!bfs.empty()) {
      auto const u{bfs.front()};
      bfs.pop();
      for (auto c{std::size_t{0}}; c < 256; ++c) {
        auto const v{m_nodes[u].next[c]};
        if (v == no_node || v == root_id) {
          m_nodes[u].next[c] = m_nodes[m_nodes[u].fail].next[c];
        } else {
          m_nodes[v].fail = m_nodes[m_nodes[u].fail].next[c];
          // Inherit dictionary-suffix matches from the failure node.
          auto const& src{m_nodes[m_nodes[v].fail].terminals};
          auto& dst{m_nodes[v].terminals};
          dst.insert(dst.end(), src.begin(), src.end());
          bfs.push(v);
        }
      }
    }
    m_built = true;
  }

  /**
   * @brief Scans \p text and reports every pattern occurrence via callback.
   *
   * Walks the automaton one byte at a time and, at each position, invokes
   * \p visit once per pattern ending there with \c (pattern_id, end_position),
   * where \c end_position is the index just past the matched substring. When
   * \p visit returns \c bool, returning \c false stops the scan early; any
   * other return type is ignored. A matcher that has not been built reports
   * nothing.
   *
   * @tparam Visitor Callable invocable with \c (std::size_t pattern_id,
   *         std::size_t end_position); may return \c bool to request early
   *         termination.
   * @param text Text to scan.
   * @param visit Callback invoked once per match.
   *
   * @pre \c build() has been called since the last \c add_pattern.
   * @post Every reported \c end_position lies in \c (0, text.size()] and
   *       \c pattern_id is a valid identifier; reports are in ascending text
   *       order.
   *
   * @complexity \c O(|text| + number of matches).
   */
  template <typename Visitor>
  auto scan(std::string_view const text, Visitor&& visit) const -> void {
    if (!m_built) {
      return;
    }
    auto cur{root_id};
    for (auto i{std::size_t{0}}; i < text.size(); ++i) {
      auto const byte{static_cast<std::uint8_t>(text[i])};
      cur = m_nodes[cur].next[byte];
      for (auto const id : m_nodes[cur].terminals) {
        auto const end_pos{i + 1};
        if constexpr (std::is_same_v<decltype(visit(id, end_pos)), bool>) {
          if (!visit(id, end_pos)) {
            return;
          }
        } else {
          visit(id, end_pos);
        }
      }
    }
  }

  /**
   * @brief Number of patterns added to the matcher.
   *
   * @return The count of patterns inserted via \c add_pattern.
   *
   * @pre None.
   * @post The matcher is unchanged.
   */
  [[nodiscard]] auto pattern_count() const noexcept -> std::size_t {
    return m_patterns.size();
  }

  /**
   * @brief Length of the pattern with identifier \p id.
   *
   * @param id Pattern identifier returned by \c add_pattern.
   *
   * @return The length in bytes of pattern \p id.
   *
   * @pre \p id is less than \c pattern_count(); a larger value is undefined
   *      behaviour.
   * @post The matcher is unchanged.
   */
  [[nodiscard]] auto pattern_length(std::size_t const id) const noexcept -> std::size_t {
    return m_patterns[id];
  }
};

}  // namespace nexenne::algorithm
