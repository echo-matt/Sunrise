#pragma once

#include <cstdint>

#include "../account/inventory/inventory_state.h"
#include "../build_data/items/details/definition.h"
#include "../build_data/items/item_catalog.h"

namespace sunrise::state::runtime::detail {

/**
 * Gives one item instance the per-instance entropy the Client reduces into a plug choice for
 * every socket whose plug set is randomized.
 *
 * The Client owns the choice: for a socket entry with no explicit state it takes one random-roll
 * byte, picked by that entry's own selector byte, modulo the entry's plug set row count. Leaving
 * the bytes zero pins every randomized socket to its plug set's first row, which is what a caller
 * that wants one exact curated roll gets by not calling this.
 *
 * Every refusal happens before the item is touched, so a refused item keeps the curated native
 * defaults whole. An item is refused when it is not a character weapon, when it is an exotic
 * (which ships one authored roll), or when it declares no usable ordinary socket block.
 *
 * @param item Authored item instance updated in place.
 * @param itemDefinition Base item definition, used to gate on bucket and tier.
 * @param itemDetail Base item detail, used to skip items with no ordinary socket block.
 * @param seed 64-bit entropy seed, mixed before the instance is folded in.
 * @return True when the item received random-roll bytes.
 */
[[nodiscard]] bool roll_random_bytes(account::inventory::Item& item,
                                     const build_data::items::Definition& itemDefinition,
                                     const build_data::items::details::Definition& itemDetail,
                                     std::uint64_t seed) noexcept;

} // namespace sunrise::state::runtime::detail
