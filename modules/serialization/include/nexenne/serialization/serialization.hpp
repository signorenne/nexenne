#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::serialization module.
 *
 * Pulls in every public header: the JSON DOM, parser, SAX, and writers; the
 * schema-driven binary reader and writer; the CBOR and MessagePack codecs; COBS
 * framing; and the versioned-payload wrapper. Include a single subsystem header
 * directly when you do not need the rest.
 */

#include <nexenne/serialization/binary/reader.hpp>
#include <nexenne/serialization/binary/writer.hpp>
#include <nexenne/serialization/cbor.hpp>
#include <nexenne/serialization/cobs.hpp>
#include <nexenne/serialization/error.hpp>
#include <nexenne/serialization/format.hpp>
#include <nexenne/serialization/json/parse.hpp>
#include <nexenne/serialization/json/sax.hpp>
#include <nexenne/serialization/json/serialize.hpp>
#include <nexenne/serialization/json/value.hpp>
#include <nexenne/serialization/json/writer.hpp>
#include <nexenne/serialization/msgpack.hpp>
#include <nexenne/serialization/versioned.hpp>
