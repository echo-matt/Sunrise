#pragma once

#include <cstdint>

#include "../account/inventory/inventory_state.h"
#include "../build_data/items/details/definition.h"
#include "../build_data/items/item_catalog.h"

namespace sunrise::state::runtime::detail {

/**
 * Authors one eligible weapon instance's random bytes, rolled ordinary-socket plugs, masterwork,
 * and owned-row offer masks from the installed build-data catalogs.
 *
 * Every item-level refusal happens before the item is touched, so a refused item keeps the curated
 * native defaults whole. Non-weapons, exotics, and definitions without a usable ordinary socket
 * block are refused. Individual malformed or unrepresentable lanes safely retain their authored
 * plug while other valid lanes may still roll.
 *
 * @param item Authored item instance updated in place.
 * @param itemDefinition Base item definition, used to gate on bucket and tier.
 * @param itemDetail Base item detail, socket, and stat data used to author the roll.
 * @param seed 64-bit entropy seed, mixed before the instance is folded in.
 * @return True when the item passed the item-level gates and received random-roll state.
 */
[[nodiscard]] bool roll_random_bytes(account::inventory::Item& item,
                                     const build_data::items::Definition& itemDefinition,
                                     const build_data::items::details::Definition& itemDetail,
                                     std::uint64_t seed) noexcept;

} // namespace sunrise::state::runtime::detail
