#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::algorithm module.
 *
 * Algorithms that are not in the C++23 standard library but are broadly useful
 * for embedded and systems work, grouped by category: binary search variants,
 * integer sorts, hashing, checksums, byte encodings, string matching, numerical
 * routines, and graph algorithms. Including this header pulls in every category;
 * include a specific subheader to take only what you need.
 */

#include <nexenne/algorithm/binary_search.hpp>
#include <nexenne/algorithm/checksum/crc.hpp>
#include <nexenne/algorithm/checksum/modular_sum.hpp>
#include <nexenne/algorithm/encoding/alphabet.hpp>
#include <nexenne/algorithm/encoding/base_n.hpp>
#include <nexenne/algorithm/encoding/cobs.hpp>
#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/algorithm/encoding/url.hpp>
#include <nexenne/algorithm/graph/a_star.hpp>
#include <nexenne/algorithm/graph/bellman_ford.hpp>
#include <nexenne/algorithm/graph/bfs.hpp>
#include <nexenne/algorithm/graph/connected_components.hpp>
#include <nexenne/algorithm/graph/dfs.hpp>
#include <nexenne/algorithm/graph/dijkstra.hpp>
#include <nexenne/algorithm/graph/floyd_warshall.hpp>
#include <nexenne/algorithm/graph/kruskal_mst.hpp>
#include <nexenne/algorithm/graph/lca.hpp>
#include <nexenne/algorithm/graph/tarjan_scc.hpp>
#include <nexenne/algorithm/graph/topological_sort.hpp>
#include <nexenne/algorithm/hash/fnv.hpp>
#include <nexenne/algorithm/hash/murmur.hpp>
#include <nexenne/algorithm/hash/xxhash.hpp>
#include <nexenne/algorithm/numerical/bisection.hpp>
#include <nexenne/algorithm/numerical/fft.hpp>
#include <nexenne/algorithm/numerical/integration.hpp>
#include <nexenne/algorithm/numerical/interpolation.hpp>
#include <nexenne/algorithm/numerical/kahan_sum.hpp>
#include <nexenne/algorithm/numerical/numerical_error.hpp>
#include <nexenne/algorithm/numerical/ode.hpp>
#include <nexenne/algorithm/numerical/online_stats.hpp>
#include <nexenne/algorithm/sort/counting_sort.hpp>
#include <nexenne/algorithm/sort/radix_sort.hpp>
#include <nexenne/algorithm/string/aho_corasick.hpp>
#include <nexenne/algorithm/string/boyer_moore.hpp>
#include <nexenne/algorithm/string/kmp.hpp>
#include <nexenne/algorithm/string/levenshtein.hpp>
#include <nexenne/algorithm/string/suffix_array.hpp>
#include <nexenne/algorithm/string/z_function.hpp>
