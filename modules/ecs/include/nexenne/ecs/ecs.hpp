#pragma once

/**
 * @file
 * @brief Umbrella header for the nexenne::ecs module.
 *
 * A sparse-set entity-component registry in the EnTT style: cache-friendly
 * component storage, recycle-safe entity handles, multi-component views, and
 * per-component lifecycle signals, all with no virtual dispatch. The pieces:
 *
 * - \c type_id : dense, RTTI-free, per-type ids used to index component
 *   storage.
 * - \c registry : owns entities and the type-erased component storages;
 *   \c create / \c destroy, \c add / \c remove / \c get / \c has, and the
 *   on-construct / on-update / on-destroy signals.
 * - \c basic_view : visits every entity carrying a set of components (with an
 *   optional exclude list), driven by the smallest matching storage.
 *
 * Pulls in every public header. For finer-grained build dependencies, include
 * the individual leaf headers under \c nexenne/ecs/ directly.
 */

#include <nexenne/ecs/format.hpp>
#include <nexenne/ecs/registry.hpp>
#include <nexenne/ecs/type_id.hpp>
#include <nexenne/ecs/view.hpp>

namespace nexenne::ecs {}
