#include "codec.h"

namespace sunrise::state::build_data::cache::records {

/** Encodes one exact item/lane rule. The old padding byte now carries the socket-entry index. */
bool encode(const items::socket_plugs::Rule& value, SocketPlugRuleRecord& record) noexcept {
    record = {};
    record.itemDefinitionIndex = value.itemDefinitionIndex;
    record.lane = value.lane;
    record.socketEntryIndex = value.socketEntryIndex;
    record.poolIndex = value.poolIndex;
    record.rollPoolIndex = value.rollPoolIndex;
    return true;
}

/** Decodes one exact item/lane rule. Every byte is meaningful, so there is nothing to check. */
bool decode(const SocketPlugRuleRecord& record, items::socket_plugs::Rule& value) noexcept {
    value = {};
    value.itemDefinitionIndex = record.itemDefinitionIndex;
    value.lane = record.lane;
    value.socketEntryIndex = record.socketEntryIndex;
    value.poolIndex = record.poolIndex;
    value.rollPoolIndex = record.rollPoolIndex;
    return true;
}

/** Encodes one pool range. Cross-row contiguity is checked at the domain boundary. */
bool encode(const items::socket_plugs::Pool& value, SocketPlugPoolRecord& record) noexcept {
    record = {value.memberOffset, value.memberCount};
    return true;
}

/** Decodes one pool range. Cross-row contiguity is checked at the domain boundary. */
bool decode(const SocketPlugPoolRecord& record, items::socket_plugs::Pool& value) noexcept {
    value = {record.memberOffset, record.memberCount};
    return true;
}

/** Encodes one native plug-definition index. */
bool encode(items::socket_plugs::Member value, SocketPlugMemberRecord& record) noexcept {
    record = {value};
    return true;
}

/** Decodes one native plug-definition index. */
bool decode(const SocketPlugMemberRecord& record, items::socket_plugs::Member& value) noexcept {
    value = record.itemDefinitionIndex;
    return true;
}

bool encode(items::socket_plugs::Pool value, SocketPlugRollPoolRecord& record) noexcept {
    record = {value.memberOffset, value.memberCount};
    return true;
}

bool decode(const SocketPlugRollPoolRecord& record, items::socket_plugs::Pool& value) noexcept {
    value = {record.memberOffset, record.memberCount};
    return true;
}

bool encode(items::socket_plugs::Member value, SocketPlugRollMemberRecord& record) noexcept {
    record = {value};
    return true;
}

bool decode(const SocketPlugRollMemberRecord& record, items::socket_plugs::Member& value) noexcept {
    value = record.itemDefinitionIndex;
    return true;
}

} // namespace sunrise::state::build_data::cache::records
