#include "sockets_lifecycle.h"

#include <atomic>

#include "socket_masks_hook.h"

namespace sunrise::client::hooks::sockets {
namespace {

std::atomic_bool g_installed{false};

} // namespace

bool install() noexcept {
    const bool masksHook = install_socket_masks_hook();
    g_installed.store(masksHook, std::memory_order_release);
    return masksHook;
}

void uninstall() noexcept {
    uninstall_socket_masks_hook();
    g_installed.store(false, std::memory_order_release);
}

bool is_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

} // namespace sunrise::client::hooks::sockets
