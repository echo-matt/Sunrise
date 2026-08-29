/**
 * Hook on the native ordinary-socket mask rebuild function.
 *
 * Inside destiny2.exe, whenever an item's inventory state, equipped state, or socket state is
 * evaluated, the engine calls item_ordinary_socket_masks_rebuild to compute activeMask (+164),
 * definitionUnlockMask (+168), blockedMask (+172), and expressionUnlockMask (+176).
 *
 * In the engine's hover tooltip builder (sub_7FF7B7D087B0), socket cells are evaluated in two
 * passes:
 * - Pass 1 evaluates definition-declared initial plugs (kind 1) gated by Filter 1
 *   (sub_7FF7B6E06D10). Filter 1 checks `!_bittest(&expressionUnlockMask, lane)`. If bit `lane` is
 *   clear (0), Filter 1 accepts the definition initial plug (the curated/default perk), adding it
 *   to the tooltip.
 * - Pass 2 evaluates the instance's own ordinary sockets (kind 0, ordinarySockets.sockets[lane])
 *   gated by Filter 1 and Filter 2 (sub_7FF7B6E06BD0).
 *
 * Because native weapon perks carry no unlock expressions, the engine's mask rebuilder clears
 * expressionUnlockMask (+176) to 0. This causes the hover tooltip to always accept the definition's
 * default curated perks during Pass 1, filling up the 6-perk tooltip buffer and hiding the
 * instance's dynamic rolled perks. Meanwhile, the Inspect screen (item_socket_cell_widget_update at
 * 0x7FF7B7DFE180) directly reads ordinarySockets.sockets[lane].plugDefinitionIndex, creating a
 * divergence between the hover preview and the inspect screen.
 *
 * This hook detours item_ordinary_socket_masks_rebuild. After the native function computes the base
 * masks, for every dynamic lane (lane >= 1) where ordinarySockets.sockets[lane] contains an
 * assigned plug, it clears bit `lane` in definitionUnlockMask (+168) and sets bit `lane` in
 * expressionUnlockMask (+176). This ensures that Filter 1 suppresses the definition's curated plug
 * during Pass 1, allowing Pass 2 to emit the instance's rolled plug into the hover tooltip so that
 * the hover preview and the inspect screen display the exact same perks.
 */

#include "socket_masks_hook.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "../../patterns/image_scan.h"

namespace sunrise::client::hooks::sockets {
namespace {

using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * Signature for item_ordinary_socket_masks_rebuild.
 * Unique in the main image.
 */
constexpr std::string_view kMasksRebuildSignatureText =
    "4C 89 44 24 ? 48 89 4C 24 ? 53 56 57 41 54 41 55 41 57 48 83 EC 58 33 C0 4C 89 74 24 ? "
    "4C 8B B4 24 ? ? ? ? 32 DB 48 89 82 ? ? ? ? 4D 8B F9 4D 8B E8";

constexpr auto kMasksRebuildSignature =
    signature<signature_length(kMasksRebuildSignatureText)>(kMasksRebuildSignatureText);

/** Function signature of native item_ordinary_socket_masks_rebuild. */
using MasksRebuildFn = __int64(__fastcall*)(void* itemDef,
                                            void* itemObject,
                                            void* socketBlock,
                                            void* invContext,
                                            void* profileContext) noexcept;

hooking::detour::Handle g_handle{};
std::atomic<MasksRebuildFn> g_original{nullptr};

/** Byte offset of OrdinarySocketBlock within native item-instance object. */
constexpr std::size_t kOrdinarySocketsOffset = 20;
/** Stride of one native OrdinarySocket lane (plugDefinitionIndex + reserved + auxiliary hashes). */
constexpr std::size_t kOrdinarySocketStride = 12;
/** Maximum ordinary socket capacity on one item instance. */
constexpr std::size_t kMaxOrdinarySockets = 12;
/** Byte offset of definitionUnlockMask within native item-instance object. */
constexpr std::size_t kDefinitionUnlockMaskOffset = 168;
/** Byte offset of expressionUnlockMask within native item-instance object. */
constexpr std::size_t kExpressionUnlockMaskOffset = 176;
/** Sentinel plug index indicating an empty/unpopulated socket lane. */
constexpr std::uint16_t kEmptyPlugIndex = 0xFFFF;

/**
 * Detoured mask rebuild: calls original, then enforces suppression of definition default plugs
 * for all dynamic lanes that carry an instance ordinary socket plug.
 */
__int64 __fastcall detour_masks_rebuild(void* itemDef,
                                        void* itemObject,
                                        void* socketBlock,
                                        void* invContext,
                                        void* profileContext) noexcept {
    const MasksRebuildFn original = g_original.load(std::memory_order_acquire);
    if (original == nullptr) {
        return 0;
    }

    const __int64 result = original(itemDef, itemObject, socketBlock, invContext, profileContext);

    if (itemObject != nullptr) {
        auto* const bytes = static_cast<std::byte*>(itemObject);
        auto* const defMask = reinterpret_cast<std::uint32_t*>(bytes + kDefinitionUnlockMaskOffset);
        auto* const exprMask =
            reinterpret_cast<std::uint32_t*>(bytes + kExpressionUnlockMaskOffset);

        // Lane 0 is the intrinsic weapon frame / archetype. Lanes 1..11 are dynamic perk,
        // magazine, sight, mod, or cosmetic sockets.
        for (std::size_t lane = 1; lane < kMaxOrdinarySockets; ++lane) {
            const auto plug = *reinterpret_cast<const std::uint16_t*>(
                bytes + kOrdinarySocketsOffset + (lane * kOrdinarySocketStride));
            if (plug != kEmptyPlugIndex) {
                *defMask &= ~(1U << lane);
                *exprMask |= (1U << lane);
            }
        }
    }

    return result;
}

} // namespace

bool install_socket_masks_hook() noexcept {
    if (g_handle.attached) {
        return true;
    }

    std::byte* const target =
        scan_main_image_unique(kMasksRebuildSignature, "item_ordinary_socket_masks_rebuild");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=hook stage=install group=sockets name=masks_rebuild result=fail "
                         "reason=target");
        return false;
    }

    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&detour_masks_rebuild)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=hook stage=install group=sockets name=masks_rebuild result=fail "
                         "reason=attach");
        return false;
    }

    g_original.store(reinterpret_cast<MasksRebuildFn>(g_handle.original),
                     std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=hook stage=install group=sockets name=masks_rebuild result=ok");
    return true;
}

void uninstall_socket_masks_hook() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
}

} // namespace sunrise::client::hooks::sockets
