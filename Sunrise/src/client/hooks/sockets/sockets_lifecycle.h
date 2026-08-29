#pragma once

namespace sunrise::client::hooks::sockets {

/** Attaches the socket hooks. */
[[nodiscard]] bool install() noexcept;

/** Detaches every socket hook. */
void uninstall() noexcept;

/** @return True while the socket hooks are attached. */
[[nodiscard]] bool is_installed() noexcept;

} // namespace sunrise::client::hooks::sockets
