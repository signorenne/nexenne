#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::algorithm module.
 *
 * Algorithms that are not in the C++23 standard library but are broadly
 * useful for embedded and systems work, grouped by category: binary search
 * variants, integer sorts, string matching, numerical routines, hashing,
 * checksums, byte encodings, and graph algorithms.
 *
 * @note Module under construction. Headers are ported in dependency-ordered
 *       phases; each category's includes are added here as that category lands.
 */

#include <nexenne/algorithm/binary_search.hpp>
#include <nexenne/algorithm/checksum/adler32.hpp>
#include <nexenne/algorithm/checksum/crc.hpp>
#include <nexenne/algorithm/encoding/alphabet.hpp>
#include <nexenne/algorithm/encoding/base_n.hpp>
#include <nexenne/algorithm/encoding/cobs.hpp>
#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/algorithm/encoding/url.hpp>
#include <nexenne/algorithm/hash/fnv.hpp>
#include <nexenne/algorithm/hash/murmur.hpp>
#include <nexenne/algorithm/hash/xxhash.hpp>
