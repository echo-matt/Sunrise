#include "socket_plug_catalog.h"

#include <algorithm>
#include <bitset>

#include "../../table.h"

namespace sunrise::state::build_data::items::socket_plugs {
namespace {

Lock g_lock;
Table<Rule, kRuleCapacity> g_rules;
Table<Pool, kPoolCapacity> g_pools;
Table<Member, kMemberCapacity> g_members;
Table<Pool, kRollPoolCapacity> g_rollPools;
Table<Member, kRollMemberCapacity> g_rollMembers;
std::bitset<details::kDefinitionCapacity> g_membership;

/** @return True when the first rule's item/lane key is strictly before the second. */
[[nodiscard]] bool rule_less(const Rule& left, const Rule& right) noexcept {
    return left.itemDefinitionIndex < right.itemDefinitionIndex
           || (left.itemDefinitionIndex == right.itemDefinitionIndex && left.lane < right.lane);
}

} // namespace

/** Clears the complete socket-plug catalog under one exclusive hold. */
void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_rules.clear();
    g_pools.clear();
    g_members.clear();
    g_rollPools.clear();
    g_rollMembers.clear();
    g_membership.reset();
}

/** Checks counts, strict rule order, contiguous pools, and sorted unique pool members. */
bool valid(std::span<const Rule> rules,
           std::span<const Pool> pools,
           std::span<const Member> members,
           std::span<const Pool> rollPools,
           std::span<const Member> rollMembers) noexcept {
    if (rules.empty() || rules.size() > kRuleCapacity || pools.empty()
        || pools.size() > kPoolCapacity || members.size() > kMemberCapacity
        || pools.front().memberOffset != 0 || pools.front().memberCount != 0 || rollPools.empty()
        || rollPools.size() > kRollPoolCapacity || rollMembers.size() > kRollMemberCapacity
        || rollPools.front().memberOffset != 0 || rollPools.front().memberCount != 0) {
        return false;
    }
    for (std::size_t index = 0; index < rules.size(); ++index) {
        const Rule& rule = rules[index];
        if ((rule.socketEntryIndex != kNoSocketEntry && rule.socketEntryIndex >= 64)
            || rule.lane >= kLaneCapacity || rule.poolIndex >= pools.size()
            || rule.rollPoolIndex >= rollPools.size()
            || (index != 0 && !rule_less(rules[index - 1], rule))) {
            return false;
        }
    }
    std::size_t expectedOffset = 0;
    for (std::size_t index = 0; index < pools.size(); ++index) {
        const Pool& pool = pools[index];
        if (pool.memberOffset != expectedOffset || (index != 0 && pool.memberCount == 0)
            || pool.memberCount > members.size() - expectedOffset) {
            return false;
        }
        const auto range = members.subspan(expectedOffset, pool.memberCount);
        if (!std::is_sorted(range.begin(), range.end())
            || std::adjacent_find(range.begin(), range.end()) != range.end()
            || std::any_of(range.begin(), range.end(), [](Member member) {
                   return member >= details::kDefinitionCapacity;
               })) {
            return false;
        }
        expectedOffset += pool.memberCount;
    }
    if (expectedOffset != members.size()) {
        return false;
    }
    expectedOffset = 0;
    for (std::size_t index = 0; index < rollPools.size(); ++index) {
        const Pool& pool = rollPools[index];
        if (pool.memberOffset != expectedOffset || (index != 0 && pool.memberCount == 0)
            || pool.memberCount > rollMembers.size() - expectedOffset) {
            return false;
        }
        const auto range = rollMembers.subspan(expectedOffset, pool.memberCount);
        if (std::any_of(range.begin(), range.end(), [](Member member) {
                return member >= details::kDefinitionCapacity;
            })) {
            return false;
        }
        expectedOffset += pool.memberCount;
    }
    return expectedOffset == rollMembers.size();
}

/** Replaces all arrays while no reader can observe a partial relation. */
bool replace(std::span<const Rule> rules,
             std::span<const Pool> pools,
             std::span<const Member> members,
             std::span<const Pool> rollPools,
             std::span<const Member> rollMembers) noexcept {
    if (!valid(rules, pools, members, rollPools, rollMembers)) {
        return false;
    }
    // Membership answers "does this definition occur in any ordinary socket at all", so it spans
    // both banks. The curated pool alone would leave a perk that only ever appears in a randomized
    // set looking unpooled, and the rolled-result search treats an unpooled plug as a roll result.
    std::bitset<details::kDefinitionCapacity> membership;
    for (const Member member : members) {
        membership.set(member);
    }
    for (const Member member : rollMembers) {
        membership.set(member);
    }
    const Lock::Exclusive guard(g_lock);
    if (!g_rules.replace(rules) || !g_pools.replace(pools) || !g_members.replace(members)
        || !g_rollPools.replace(rollPools) || !g_rollMembers.replace(rollMembers)) {
        return false;
    }
    g_membership = membership;
    return true;
}

/** Performs an exact item/lane rule lookup followed by a binary search in its plug pool. */
bool allowed(std::uint16_t itemDefinitionIndex,
             std::uint8_t lane,
             std::uint16_t plugDefinitionIndex) noexcept {
    if (lane >= kLaneCapacity) {
        return false;
    }
    const Lock::Shared guard(g_lock);
    const auto rules = g_rules.rows();
    const auto pools = g_pools.rows();
    const auto members = g_members.rows();
    const Rule key{itemDefinitionIndex, lane, 0, 0, 0};
    const auto found = std::lower_bound(rules.begin(), rules.end(), key, rule_less);
    if (found == rules.end() || found->itemDefinitionIndex != itemDefinitionIndex
        || found->lane != lane || found->poolIndex >= pools.size()) {
        return false;
    }
    const Pool& pool = pools[found->poolIndex];
    if (pool.memberOffset > members.size()
        || pool.memberCount > members.size() - pool.memberOffset) {
        return false;
    }
    const auto range = members.subspan(pool.memberOffset, pool.memberCount);
    return std::binary_search(range.begin(), range.end(), plugDefinitionIndex);
}

/** Walks the members of one lane's pool under a shared hold. */
bool visit_pool(std::uint16_t itemDefinitionIndex,
                std::uint8_t lane,
                MemberVisitor visitor,
                void* context) noexcept {
    if (lane >= kLaneCapacity || visitor == nullptr) {
        return false;
    }
    const Lock::Shared guard(g_lock);
    const auto rules = g_rules.rows();
    const auto pools = g_pools.rows();
    const auto members = g_members.rows();
    const Rule key{itemDefinitionIndex, lane, 0, 0, 0};
    const auto found = std::lower_bound(rules.begin(), rules.end(), key, rule_less);
    if (found == rules.end() || found->itemDefinitionIndex != itemDefinitionIndex
        || found->lane != lane || found->poolIndex >= pools.size()) {
        return false;
    }
    const Pool& pool = pools[found->poolIndex];
    if (pool.memberOffset > members.size()
        || pool.memberCount > members.size() - pool.memberOffset) {
        return false;
    }
    for (const Member member : members.subspan(pool.memberOffset, pool.memberCount)) {
        if (!visitor(context, member)) {
            return false;
        }
    }
    return true;
}

/** Walks one lane's native-order randomized roll pool under a shared hold. */
bool visit_roll_pool(std::uint16_t itemDefinitionIndex,
                     std::uint8_t lane,
                     MemberVisitor visitor,
                     void* context) noexcept {
    if (lane >= kLaneCapacity || visitor == nullptr) {
        return false;
    }
    const Lock::Shared guard(g_lock);
    const auto rules = g_rules.rows();
    const auto rollPools = g_rollPools.rows();
    const auto rollMembers = g_rollMembers.rows();
    const Rule key{itemDefinitionIndex, lane, 0, 0, 0};
    const auto found = std::lower_bound(rules.begin(), rules.end(), key, rule_less);
    if (found == rules.end() || found->itemDefinitionIndex != itemDefinitionIndex
        || found->lane != lane || found->rollPoolIndex >= rollPools.size()) {
        return false;
    }
    const Pool& pool = rollPools[found->rollPoolIndex];
    if (pool.memberOffset > rollMembers.size()
        || pool.memberCount > rollMembers.size() - pool.memberOffset) {
        return false;
    }
    for (const Member member : rollMembers.subspan(pool.memberOffset, pool.memberCount)) {
        if (!visitor(context, member)) {
            return false;
        }
    }
    return true;
}

/** Returns the socket-entry index a rolled lane resolves through, or kNoSocketEntry. */
std::uint8_t socket_entry_index(std::uint16_t itemDefinitionIndex, std::uint8_t lane) noexcept {
    if (lane >= kLaneCapacity) {
        return kNoSocketEntry;
    }
    const Lock::Shared guard(g_lock);
    const auto rules = g_rules.rows();
    const Rule key{itemDefinitionIndex, lane, 0, 0, 0};
    const auto found = std::lower_bound(rules.begin(), rules.end(), key, rule_less);
    if (found == rules.end() || found->itemDefinitionIndex != itemDefinitionIndex
        || found->lane != lane) {
        return kNoSocketEntry;
    }
    return found->socketEntryIndex;
}

/** Answers whether one definition occurs in any installed ordinary-socket plug pool. */
bool contains(Member plugDefinitionIndex) noexcept {
    const Lock::Shared guard(g_lock);
    return plugDefinitionIndex < g_membership.size() && g_membership.test(plugDefinitionIndex);
}

/** Copies the related arrays under the same shared hold. */
bool snapshot(std::span<Rule> rules,
              std::size_t& ruleCount,
              std::span<Pool> pools,
              std::size_t& poolCount,
              std::span<Member> members,
              std::size_t& memberCount,
              std::span<Pool> rollPools,
              std::size_t& rollPoolCount,
              std::span<Member> rollMembers,
              std::size_t& rollMemberCount) noexcept {
    ruleCount = 0;
    poolCount = 0;
    memberCount = 0;
    rollPoolCount = 0;
    rollMemberCount = 0;
    const Lock::Shared guard(g_lock);
    return g_rules.snapshot(rules, ruleCount) && g_pools.snapshot(pools, poolCount)
           && g_members.snapshot(members, memberCount)
           && g_rollPools.snapshot(rollPools, rollPoolCount)
           && g_rollMembers.snapshot(rollMembers, rollMemberCount);
}

/** Reports the published rule count under the catalog lock. */
std::size_t rule_count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_rules.count();
}

} // namespace sunrise::state::build_data::items::socket_plugs
