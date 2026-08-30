#pragma once

namespace sunrise::client::hooks::sockets {

/**
 * Installs the detour on the native item ordinary-socket mask rebuild function.
 * @return True when the hook is installed or already attached.
 */
[[nodiscard]] bool install_socket_masks_hook() noexcept;

/** Detaches the mask rebuild hook and drops its trampoline. */
void uninstall_socket_masks_hook() noexcept;

} // namespace sunrise::client::hooks::sockets
