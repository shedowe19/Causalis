#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace causalis::vault {
enum class Provider { BitwardenUS, BitwardenEU, Vaultwarden };
struct ServerConfig {
    Provider provider{Provider::BitwardenUS};
    std::string origin{"https://vault.bitwarden.com"};
};
struct ServerSelection { bool ok{}; ServerConfig server; std::string error; };
// Only HTTPS origins; no userinfo, query, fragment, path, or TLS bypass.
ServerSelection select_server(Provider provider, std::string_view origin = {});

enum class VaultState { Unknown, Unauthenticated, Locked, Unlocked };
struct Status {
    VaultState state{VaultState::Unknown};
    std::string server_origin;
    std::string last_sync;
};
struct StatusResult { bool ok{}; Status value; std::string error; };
// Parse a bounded flat CLI status object and drop userEmail/userId/unknown fields.
StatusResult parse_status(std::string_view json);
std::string status_json(const Status& status);

enum class Action { Status, ConfigureServer, Sync, Lock, Logout };
struct CommandPlan {
    std::string profile;
    std::vector<std::string> arguments;
};
struct PlanResult { bool ok{}; CommandPlan plan; std::string error; };
// No login/unlock/get/list/export/serve or arbitrary command interface exists.
PlanResult make_plan(std::string_view profile, Action action,
                     const ServerConfig& server = {},
                     VaultState observed_state = VaultState::Unknown);
} // namespace causalis::vault
