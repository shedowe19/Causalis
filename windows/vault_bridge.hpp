#pragma once
#include "causalis/vault_policy.hpp"
#include <string>

namespace causalis::vault {
struct Result { bool ok{}; std::string output, error; };
// Blocking bounded operations: call from a browser-owned worker, never the UI thread.
// appdata is LOCALAPPDATA\Causalis\workspaces\{Privat|Arbeit|Homelab}\BitwardenCLI.
// The official, externally installed bw.exe must be explicitly selected by the user.
Result run_status(const std::wstring& exe, const std::wstring& appdata);
Result configure_server(const std::wstring& exe, const std::wstring& appdata, const ServerConfig& server);
Result lock_vault(const std::wstring& exe, const std::wstring& appdata);
Result sync_vault(const std::wstring& exe, const std::wstring& appdata);
Result logout_vault(const std::wstring& exe, const std::wstring& appdata);
} // namespace causalis::vault
