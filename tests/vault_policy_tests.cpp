#include "causalis/vault_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace causalis::vault;
namespace {
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}
int main() {
    check(select_server(Provider::BitwardenUS).server.origin == "https://vault.bitwarden.com", "US exact default");
    check(select_server(Provider::BitwardenEU).server.origin == "https://vault.bitwarden.eu", "EU exact default");
    check(select_server(Provider::BitwardenUS, "HTTPS://VAULT.BITWARDEN.COM:0443/").ok, "US normalization");
    check(!select_server(Provider::BitwardenUS, "https://vault.bitwarden.com.evil.test").ok, "provider suffix spoof");
    check(!select_server(Provider::BitwardenEU, "https://vault.bitwarden.com").ok, "provider mismatch");
    check(!select_server(static_cast<Provider>(99)).ok, "invalid provider enum");
    auto selfhost = select_server(Provider::Vaultwarden, "HTTPS://Vault.Home:008443/");
    check(selfhost.ok && selfhost.server.origin == "https://vault.home:8443", "self-host exact normalized origin");
    for (const std::string input : {
        "", "http://vault.home", "https://", "https://name:pass@vault.home", "https://vault.home?token=secret",
        "https://vault.home/#fragment", "https://vault.home/path", "https://vault.home//", "https://vault.home\\path",
        "https://vault.home\n", "https://vault.home%2e", "https://vault.home:", "https://vault.home:0", "https://vault.home:65536",
        "https://vault.home:443:443", "https://vault..home", "https://-vault.home", "https://vault-.home", "https://vault.home.",
        "https://127.1", "https://0127.0.0.1", "https://256.0.0.1", "https://[::1%25eth0]", "https://[:::1]", "https://[::1"}) {
        check(!select_server(Provider::Vaultwarden, input).ok, "reject malformed/injected server");
    }
    check(select_server(Provider::Vaultwarden, "https://127.0.0.1:8443").ok, "strict IPv4");
    auto v6a = select_server(Provider::Vaultwarden, "https://[2001:DB8::1]:443/");
    auto v6b = select_server(Provider::Vaultwarden, "https://[2001:db8:0:0:0:0:0:1]");
    check(v6a.ok && v6b.ok && v6a.server.origin == v6b.server.origin, "equivalent IPv6 exact canonical origin");
    check(select_server(Provider::Vaultwarden, "https://[::]").ok, "IPv6 all zeros");
    const auto locked = parse_status(R"({"serverUrl":"https://vault.home/","lastSync":"2026-10-09T07:11:10.123Z","userEmail":"private@example.test","userId":"not-public","status":"locked"})");
    check(locked.ok && locked.value.state == VaultState::Locked, "official status object");
    auto summary = status_json(locked.value);
    check(summary.find("private") == std::string::npos && summary.find("userId") == std::string::npos && summary.find("not-public") == std::string::npos, "status drops account identity");
    check(summary.find("https://vault.home") != std::string::npos, "status keeps validated origin");
    const auto unauthenticated = parse_status(R"({"serverUrl":null,"lastSync":null,"userEmail":null,"userId":null,"status":"unauthenticated"})");
    check(unauthenticated.ok && unauthenticated.value.state == VaultState::Unauthenticated, "logged-out official object");
    check(unauthenticated.value.server_origin == "https://vault.bitwarden.com", "CLI null default server");
    check(parse_status(R"({"status":"unlocked","serverUrl":"https://vault.bitwarden.eu"})").ok, "unlocked state parser");
    for (const std::string input : {
        "", "[]", "{}", "{\"status\":\"not-a-status\"}", "{\"status\":\"locked\",\"status\":\"unauthenticated\"}",
        "{\"status\":\"locked\",}", "{\"status\":\"locked\"} trailing", "{\"status\":\"locked\",\"serverUrl\":\"https://x@home\"}",
        "{\"status\":\"locked\",\"lastSync\":\"arbitrary\\ntext\"}", "{\"status\":\"unauthenticated\\qnull}",
        "{\"status\":null}", "{\"status\":\"locked\",\"unknown\":{}}", "{\"status\":\"locked\",\"lastSync\":\"2026-10-09T07:11:102Z\"}"}) {
        check(!parse_status(input).ok, "reject invalid status and malformed JSON");
    }
    check(!parse_status(std::string(17000, 'x')).ok, "status bound");
    check(status_json({VaultState::Locked,"https://vault.home\"injection",{}}) == "{\"status\":\"unknown\"}", "summary cannot echo injected origin");
    for (const std::string profile : {"", "../Privat", "Privat/Arbeit", "Privat\\Arbeit", "--session", "Privat;get", "\nPrivat", ".", ".."})
        check(!make_plan(profile, Action::Status).ok, "invalid profile identifier");
    for (Action action : {Action::Status, Action::Sync, Action::Lock, Action::Logout}) {
        const auto plan = make_plan("Homelab", action);
        check(plan.ok && plan.plan.profile == "Homelab" && plan.plan.arguments.size() == 2 && plan.plan.arguments.back() == "--nointeraction", "profile-scoped allowlisted noninteractive action");
    }
    check(!make_plan("Privat", static_cast<Action>(99)).ok, "invalid action enum");
    for (const auto state : {VaultState::Unknown, VaultState::Locked, VaultState::Unlocked})
        check(!make_plan("Privat", Action::ConfigureServer, selfhost.server, state).ok, "server change gated to logged-out state");
    const auto config = make_plan("Homelab", Action::ConfigureServer, selfhost.server, unauthenticated.value.state);
    check(config.ok && config.plan.arguments == std::vector<std::string>{"config", "server", "https://vault.home:8443", "--nointeraction"}, "exact config plan after parsed logged-out status");
    check(!make_plan("Privat", Action::ConfigureServer, {Provider::BitwardenUS,"https://evil.test"}, VaultState::Unauthenticated).ok, "mutated public server config rejected");
    std::cout << "Vault policy: " << checks << " checks passed.\n";
}
