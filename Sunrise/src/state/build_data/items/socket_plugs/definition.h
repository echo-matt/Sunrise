#pragma once

#include <cstddef>
#include <cstdint>

#include "../details/definition.h"

namespace sunrise::state::build_data::items::socket_plugs {

/** Ordinary item instances expose at most 12 socket lanes. */
inline constexpr std::size_t kLaneCapacity = details::kInitialPlugCapacity;
/** At most one exact pool rule is retained for each installed item and ordinary socket lane. */
inline constexpr std::size_t kRuleCapacity = details::kDefinitionCapacity * kLaneCapacity;
/** Pool zero is the shared empty pool, in addition to at most one unique pool per rule. */
inline constexpr std::size_t kPoolCapacity = kRuleCapacity + 1;
/** Four million 16-bit members bound the deduplicated installed-build relation to 8 MiB. */
inline constexpr std::size_t kMemberCapacity = 1U << 22U;
/** Pool zero is always the canonical empty pool. */
inline constexpr std::uint32_t kEmptyPoolIndex = 0;
/** Roll-pool zero is the canonical empty native-order roll pool. */
inline constexpr std::uint32_t kEmptyRollPoolIndex = 0;
/** At most one native-order roll pool is retained per installed rule. */
inline constexpr std::size_t kRollPoolCapacity = kRuleCapacity + 1;
/** Four million 16-bit roll members bound the native-order relation. */
inline constexpr std::size_t kRollMemberCapacity = 1U << 22U;

/** Canonically marks a lane with no matching socket entry (the common intrinsic case). */
inline constexpr std::uint8_t kNoSocketEntry = 0xFF;
/** One installed item socket and the exact deduplicated plug pool it accepts. */
struct Rule {
    std::uint16_t itemDefinitionIndex{};
    std::uint8_t lane{};
    /** Socket-entry index whose roll pool is this lane's roll pool, or kNoSocketEntry. */
    std::uint8_t socketEntryIndex{};
    std::uint32_t poolIndex{};
    /** Native-order randomized roll-set pool for the lane, or kEmptyRollPoolIndex. */
    std::uint32_t rollPoolIndex{};
};

/** One contiguous range in the flat, sorted plug-definition index bank. */
struct Pool {
    std::uint32_t memberOffset{};
    std::uint32_t memberCount{};
};

/** Native item-definition index of one allowed plug. */
using Member = std::uint16_t;

/** Called once per pool member; returning false stops the walk. */
using MemberVisitor = bool (*)(void* context, Member plugDefinitionIndex) noexcept;

} // namespace sunrise::state::build_data::items::socket_plugs
