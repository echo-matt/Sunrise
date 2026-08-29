#include "package_socket_plug_build.h"

#include <algorithm>
#include <limits>

#include "../../../../state/build_data/runtime.h"

namespace sunrise::client::content::items::packages {
namespace {

/** Sundial/native category families whose socket seed expands to every plug in that family. */
constexpr std::array<std::uint32_t, 3> kExpandableCategories{
    0xB134761EU,
    0x87727F34U,
    0x6C863692U,
};
/** Tracker sockets synthesize these three safe plug choices by socket type. */
constexpr std::array<std::uint32_t, 3> kTrackerPlugHashes{
    2'285'418'970U,
    2'302'094'943U,
    38'912'240U,
};
/** Native ordinary socket type whose choices are the synthetic tracker set. */
constexpr std::uint16_t kTrackerSocketType = 518;
/** FNV-1a constants make pool fingerprints stable and cheap. */
constexpr std::uint64_t kHashOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;

/** Visitor adapter that appends one list member to a bounded lane candidate. */
struct VisitorContext {
    SocketPlugBuild* build{};
    std::size_t itemDefinitionCount{};
};

/** @return Whether the package-provided member was accepted into bounded scratch. */
[[nodiscard]] bool visit_member(void* opaque, std::uint32_t itemDefinitionIndex) noexcept {
    auto& context = *static_cast<VisitorContext*>(opaque);
    return context.build != nullptr
           && context.build->add(itemDefinitionIndex, context.itemDefinitionCount);
}

/** @return Whether the package-provided roll member was accepted into bounded roll scratch. */
[[nodiscard]] bool visit_roll_member(void* opaque, std::uint32_t itemDefinitionIndex) noexcept {
    auto& context = *static_cast<VisitorContext*>(opaque);
    return context.build != nullptr
           && context.build->add_roll(itemDefinitionIndex, context.itemDefinitionCount);
}

} // namespace

/** Returns the compact 1-based code of one native category-expansion family. */
std::uint8_t special_plug_category(std::uint32_t categoryHash) noexcept {
    for (std::size_t index = 0; index < kExpandableCategories.size(); ++index) {
        if (kExpandableCategories[index] == categoryHash) {
            return static_cast<std::uint8_t>(index + 1);
        }
    }
    return 0;
}

/** Allocates the bounded build state and indexes expansion/tracker plug definitions. */
bool SocketPlugBuild::prepare(
    std::span<const std::uint8_t> specialCategories,
    std::span<const state::build_data::items::Definition> itemDefinitions) noexcept {
    release();
    if (specialCategories.size() < itemDefinitions.size()
        || itemDefinitions.size() > state::build_data::items::kDefinitionCapacity) {
        return false;
    }
    rules_.assign(socket_plugs::kRuleCapacity, {});
    pools_.assign(socket_plugs::kPoolCapacity, {});
    members_.assign(socket_plugs::kMemberCapacity, {});
    candidates_.assign(state::build_data::items::kDefinitionCapacity, {});
    rollPools_.assign(socket_plugs::kRollPoolCapacity, {});
    rollMembers_.assign(socket_plugs::kRollMemberCapacity, {});
    rollCandidates_.assign(state::build_data::items::kDefinitionCapacity, {});
    categoryMembers_.assign(kCategoryCount * state::build_data::items::kDefinitionCapacity, {});
    lookup_.assign(kLookupCapacity, {});
    rollLookup_.assign(kLookupCapacity, {});
    pools_[socket_plugs::kEmptyPoolIndex] = {};
    poolCount_ = 1;
    rollPools_[socket_plugs::kEmptyRollPoolIndex] = {};
    rollPoolCount_ = 1;
    for (std::size_t item = 0; item < itemDefinitions.size(); ++item) {
        const std::uint8_t category = specialCategories[item];
        if (category != 0 && category <= kCategoryCount) {
            const std::size_t family = category - 1;
            categoryMembers_[family * state::build_data::items::kDefinitionCapacity
                             + categoryCounts_[family]++] = static_cast<std::uint16_t>(item);
        }
        for (const std::uint32_t trackerHash : kTrackerPlugHashes) {
            if (itemDefinitions[item].definitionHash != trackerHash) {
                continue;
            }
            if (trackerCount_ >= trackerMembers_.size()) {
                release();
                return false;
            }
            trackerMembers_[trackerCount_++] = static_cast<std::uint16_t>(item);
        }
    }
    return true;
}

/** Appends one package member after enforcing the installed item-table bound. */
bool SocketPlugBuild::add(std::uint32_t itemDefinitionIndex,
                          std::size_t itemDefinitionCount) noexcept {
    if (candidates_.empty() || itemDefinitionIndex >= itemDefinitionCount
        || itemDefinitionIndex >= state::build_data::items::kDefinitionCapacity
        || candidateCount_ >= state::build_data::items::kDefinitionCapacity) {
        return false;
    }
    candidates_[candidateCount_++] = static_cast<std::uint16_t>(itemDefinitionIndex);
    return true;
}

bool SocketPlugBuild::add_roll(std::uint32_t itemDefinitionIndex,
                               std::size_t itemDefinitionCount) noexcept {
    if (rollCandidates_.empty() || itemDefinitionIndex >= itemDefinitionCount
        || itemDefinitionIndex >= state::build_data::items::kDefinitionCapacity
        || rollCandidateCount_ >= state::build_data::items::kDefinitionCapacity) {
        return false;
    }
    rollCandidates_[rollCandidateCount_++] = static_cast<std::uint16_t>(itemDefinitionIndex);
    return true;
}

/** Expands special category seeds, sorts/deduplicates, then interns one exact pool. */
bool SocketPlugBuild::intern(std::uint32_t& poolIndex) noexcept {
    poolIndex = socket_plugs::kEmptyPoolIndex;
    if (candidates_.empty() || categoryMembers_.empty() || lookup_.empty()) {
        return false;
    }
    std::sort(candidates_.data(), candidates_.data() + candidateCount_);
    candidateCount_ = static_cast<std::size_t>(
        std::unique(candidates_.data(), candidates_.data() + candidateCount_) - candidates_.data());
    std::array<bool, kCategoryCount> expand{};
    // Category codes were indexed by native item definition index during prepare().
    for (std::size_t family = 0; family < kCategoryCount; ++family) {
        const auto* familyMembers =
            categoryMembers_.data() + family * state::build_data::items::kDefinitionCapacity;
        for (std::size_t seed = 0; seed < candidateCount_ && !expand[family]; ++seed) {
            expand[family] = std::binary_search(
                familyMembers, familyMembers + categoryCounts_[family], candidates_[seed]);
        }
    }
    for (std::size_t family = 0; family < kCategoryCount; ++family) {
        if (!expand[family]) {
            continue;
        }
        if (categoryCounts_[family]
            > state::build_data::items::kDefinitionCapacity - candidateCount_) {
            return false;
        }
        const auto* first =
            categoryMembers_.data() + family * state::build_data::items::kDefinitionCapacity;
        std::copy_n(first, categoryCounts_[family], candidates_.data() + candidateCount_);
        candidateCount_ += categoryCounts_[family];
    }
    std::sort(candidates_.data(), candidates_.data() + candidateCount_);
    candidateCount_ = static_cast<std::size_t>(
        std::unique(candidates_.data(), candidates_.data() + candidateCount_) - candidates_.data());
    if (candidateCount_ == 0) {
        return true;
    }
    return intern_into(Bank{pools_,
                            members_,
                            lookup_,
                            poolCount_,
                            memberCount_,
                            socket_plugs::kPoolCapacity,
                            socket_plugs::kMemberCapacity},
                       {candidates_.data(), candidateCount_},
                       poolIndex);
}

/** Finds the pool an identical sequence already occupies, or appends one for it. */
bool SocketPlugBuild::intern_into(Bank bank,
                                  std::span<const socket_plugs::Member> candidates,
                                  std::uint32_t& poolIndex) noexcept {
    std::uint64_t fingerprint = kHashOffsetBasis;
    for (const socket_plugs::Member member : candidates) {
        std::uint16_t value = member;
        for (std::size_t byte = 0; byte < sizeof value; ++byte) {
            fingerprint ^= static_cast<std::uint8_t>(value);
            fingerprint *= kHashPrime;
            value >>= 8U;
        }
    }
    fingerprint ^= candidates.size();
    fingerprint *= kHashPrime;
    static_assert((kLookupCapacity & (kLookupCapacity - 1)) == 0);
    const std::size_t start = static_cast<std::size_t>(fingerprint) & (kLookupCapacity - 1);
    for (std::size_t probe = 0; probe < kLookupCapacity; ++probe) {
        PoolLookup& slot = bank.lookup[(start + probe) & (kLookupCapacity - 1)];
        if (slot.poolIndex == UINT32_MAX) {
            if (bank.poolCount >= bank.poolCapacity
                || candidates.size() > bank.memberCapacity - bank.memberCount) {
                return false;
            }
            poolIndex = static_cast<std::uint32_t>(bank.poolCount);
            bank.pools[bank.poolCount++] = {static_cast<std::uint32_t>(bank.memberCount),
                                            static_cast<std::uint32_t>(candidates.size())};
            std::copy_n(
                candidates.data(), candidates.size(), bank.members.data() + bank.memberCount);
            bank.memberCount += candidates.size();
            slot = {fingerprint, poolIndex};
            return true;
        }
        if (slot.fingerprint != fingerprint || slot.poolIndex >= bank.poolCount) {
            continue;
        }
        const socket_plugs::Pool& pool = bank.pools[slot.poolIndex];
        if (pool.memberCount == candidates.size()
            && std::equal(
                candidates.begin(), candidates.end(), bank.members.data() + pool.memberOffset)) {
            poolIndex = slot.poolIndex;
            return true;
        }
    }
    return false;
}

/** Interns the current native-order roll candidate as one shared roll pool. */
bool SocketPlugBuild::intern_roll(std::uint32_t& rollPoolIndex) noexcept {
    rollPoolIndex = socket_plugs::kEmptyRollPoolIndex;
    if (rollCandidateCount_ == 0 || rollPools_.empty() || rollMembers_.empty()
        || rollLookup_.empty()) {
        return true;
    }
    // Row order is this bank's identity: the row a plug sits on is what the rolled instance
    // stores, so the sequence is interned exactly as the package published it, never sorted or
    // deduplicated the way a curated pool is. Whole weapon families share one randomized set, so
    // reusing the matching pool is what keeps the member bank off its ceiling.
    return intern_into(Bank{rollPools_,
                            rollMembers_,
                            rollLookup_,
                            rollPoolCount_,
                            rollMemberCount_,
                            socket_plugs::kRollPoolCapacity,
                            socket_plugs::kRollMemberCapacity},
                       {rollCandidates_.data(), rollCandidateCount_},
                       rollPoolIndex);
}

/** Extracts every exact ordinary socket pool of one installed item definition. */
bool SocketPlugBuild::append(const tables::items::Row& item,
                             std::span<const std::byte> itemDefinition,
                             std::span<const std::byte> plugSetTable,
                             std::size_t itemDefinitionCount) noexcept {
    if (rules_.empty() || item.definitionIndex >= itemDefinitionCount
        || item.socketCount > socket_plugs::kLaneCapacity) {
        return false;
    }
    bool complete = true;
    for (std::uint8_t lane = 0; lane < item.socketCount; ++lane) {
        candidateCount_ = 0;
        VisitorContext visitor{this, itemDefinitionCount};
        // The allowed pool is the curated offer set (embedded + reusable + initial), never
        // the randomized roll set, which is separately interned as the lane's roll pool.
        bool laneValid = tables::items::visit_allowed_plugs(
            itemDefinition, plugSetTable, lane, visit_member, &visitor, false);
        if (laneValid && item.initialPlugs[lane] != tables::items::kUnavailablePlug) {
            laneValid = add(item.initialPlugs[lane], itemDefinitionCount);
        }
        if (laneValid && item.socketTypes[lane] == kTrackerSocketType) {
            for (std::size_t tracker = 0; tracker < trackerCount_ && laneValid; ++tracker) {
                laneValid = add(trackerMembers_[tracker], itemDefinitionCount);
            }
        }
        std::uint32_t poolIndex = socket_plugs::kEmptyPoolIndex;
        laneValid = laneValid && intern(poolIndex);
        if (!laneValid || ruleCount_ >= socket_plugs::kRuleCapacity) {
            ++skipped_;
            complete = false;
            continue;
        }

        // The roll pool is an addition to the lane, not a condition on it. A lane whose randomized
        // set cannot be read, or whose members no longer fit the roll bank, keeps its curated rule
        // against the empty roll pool: dropping the rule instead would take the lane's allowed
        // pool with it and stop ordinary socket insertion working on that lane at all.
        std::uint32_t rollPoolIndex = socket_plugs::kEmptyRollPoolIndex;
        rollCandidateCount_ = 0;
        VisitorContext rollVisitor{this, itemDefinitionCount};
        if (!tables::items::visit_roll_plugs(
                itemDefinition, plugSetTable, lane, visit_roll_member, &rollVisitor)
            || !intern_roll(rollPoolIndex)) {
            rollCandidateCount_ = 0;
            rollPoolIndex = socket_plugs::kEmptyRollPoolIndex;
            complete = false;
        }
        rules_[ruleCount_++] = {item.definitionIndex, lane, 0, poolIndex, rollPoolIndex};
    }
    return complete;
}

/** Publishes the bounded relation and releases all transient interning memory. */
bool SocketPlugBuild::publish() noexcept {
    const bool published = !rules_.empty() && !pools_.empty() && !members_.empty()
                           && state::build_data::publish_socket_plug_rules(
                               std::span(rules_.data(), ruleCount_),
                               std::span(pools_.data(), poolCount_),
                               std::span(members_.data(), memberCount_),
                               std::span(rollPools_.data(), rollPoolCount_),
                               std::span(rollMembers_.data(), rollMemberCount_));
    release();
    return published;
}

/** Reports how many lanes failed closed during extraction. */
std::size_t SocketPlugBuild::skipped() const noexcept {
    return skipped_;
}

std::size_t SocketPlugBuild::rule_count() const noexcept {
    return ruleCount_;
}

std::size_t SocketPlugBuild::pool_count() const noexcept {
    return poolCount_;
}

std::size_t SocketPlugBuild::member_count() const noexcept {
    return memberCount_;
}

/** Drops all heap-backed extraction scratch and resets every count. */
void SocketPlugBuild::release() noexcept {
    rules_.clear();
    rules_.shrink_to_fit();
    pools_.clear();
    pools_.shrink_to_fit();
    members_.clear();
    members_.shrink_to_fit();
    candidates_.clear();
    candidates_.shrink_to_fit();
    rollPools_.clear();
    rollPools_.shrink_to_fit();
    rollMembers_.clear();
    rollMembers_.shrink_to_fit();
    rollCandidates_.clear();
    rollCandidates_.shrink_to_fit();
    categoryMembers_.clear();
    categoryMembers_.shrink_to_fit();
    lookup_.clear();
    lookup_.shrink_to_fit();
    rollLookup_.clear();
    rollLookup_.shrink_to_fit();
    categoryCounts_ = {};
    trackerMembers_ = {};
    trackerCount_ = 0;
    ruleCount_ = 0;
    poolCount_ = 0;
    memberCount_ = 0;
    candidateCount_ = 0;
    rollPoolCount_ = 0;
    rollMemberCount_ = 0;
    rollCandidateCount_ = 0;
    skipped_ = 0;
}

} // namespace sunrise::client::content::items::packages
