#include "causalis/vault_policy.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <set>

namespace causalis::vault {
namespace {
constexpr std::size_t max_status_bytes = 16384;
bool ascii_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
std::string lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}
bool ipv6(std::string_view host, std::string& canonical) {
    // An explicit, conservative subset: hexadecimal IPv6, no zone IDs or embedded IPv4.
    const auto gap = host.find("::");
    if (gap != std::string_view::npos && host.find("::", gap + 2) != std::string_view::npos) return false;
    std::vector<unsigned> left, right;
    auto groups = [](std::string_view part, std::vector<unsigned>& out) {
        if (part.empty()) return true;
        while (true) {
            const auto end = part.find(':');
            const auto group = part.substr(0, end);
            if (group.empty() || group.size() > 4) return false;
            unsigned value{};
            const auto parsed = std::from_chars(group.data(), group.data() + group.size(), value, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != group.data() + group.size() || value > 65535) return false;
            out.push_back(value);
            if (end == std::string_view::npos) break;
            part.remove_prefix(end + 1);
        }
        return true;
    };
    if (gap == std::string_view::npos) {
        if (!groups(host, left) || left.size() != 8) return false;
    } else {
        if (!groups(host.substr(0, gap), left) || !groups(host.substr(gap + 2), right) || left.size() + right.size() >= 8) return false;
        left.resize(8 - right.size(), 0);
        left.insert(left.end(), right.begin(), right.end());
    }
    // Expanded lower-case hex is unambiguous and stable across equivalent inputs.
    canonical = "[";
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (i) canonical += ':';
        std::array<char, 4> text{};
        const auto p = std::to_chars(text.data(), text.data() + text.size(), left[i], 16);
        canonical.append(text.data(), p.ptr);
    }
    canonical += ']';
    return true;
}
bool normalize_origin(std::string_view input, std::string& normalized) {
    if (input.size() < 9 || input.size() > 512 || lower(input.substr(0, 8)) != "https://") return false;
    for (unsigned char c : input) if (c <= 32 || c >= 127 || c == '\\' || c == '%' || c == '@' || c == '?' || c == '#') return false;
    auto authority = input.substr(8);
    if (authority.ends_with('/')) authority.remove_suffix(1);
    if (authority.empty() || authority.find('/') != std::string_view::npos) return false;
    std::string hostname;
    std::string_view port;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos || !ipv6(authority.substr(1, close - 1), hostname)) return false;
        const auto suffix = authority.substr(close + 1);
        if (!suffix.empty()) { if (suffix.front() != ':') return false; port = suffix.substr(1); if (port.empty()) return false; }
    } else {
        const auto colon = authority.find(':');
        const auto host = authority.substr(0, colon);
        if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') return false;
        std::size_t begin{};
        while (begin < host.size()) {
            const auto dot = host.find('.', begin);
            const auto label = host.substr(begin, dot == std::string_view::npos ? host.size() - begin : dot - begin);
            if (label.empty() || label.size() > 63 || !ascii_alnum(label.front()) || !ascii_alnum(label.back())) return false;
            for (const char c : label) if (!ascii_alnum(c) && c != '-') return false;
            if (dot == std::string_view::npos) break;
            begin = dot + 1;
        }
        hostname = lower(host);
        if (std::all_of(host.begin(), host.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; })) {
            std::size_t start{}; unsigned count{};
            while (true) {
                const auto end = host.find('.', start);
                const auto octet = host.substr(start, end == std::string_view::npos ? host.size() - start : end - start);
                if (octet.empty() || (octet.size() > 1 && octet.front() == '0')) return false;
                unsigned n{}; const auto p = std::from_chars(octet.data(), octet.data() + octet.size(), n);
                if (p.ec != std::errc{} || p.ptr != octet.data() + octet.size() || n > 255 || ++count > 4) return false;
                if (end == std::string_view::npos) break;
                start = end + 1;
            }
            if (count != 4) return false;
        }
        if (colon != std::string_view::npos) { port = authority.substr(colon + 1); if (port.empty()) return false; }
    }
    normalized = "https://" + hostname;
    if (!port.empty()) {
        for (char c : port) if (c < '0' || c > '9') return false;
        unsigned n{}; const auto p = std::from_chars(port.data(), port.data() + port.size(), n);
        if (p.ec != std::errc{} || p.ptr != port.data() + port.size() || n == 0 || n > 65535) return false;
        if (n != 443) normalized += ':' + std::to_string(n);
    }
    return true;
}
class JsonReader {
public:
    explicit JsonReader(std::string_view s) : text(s) {}
    void ws() { while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' || text[pos] == '\t')) ++pos; }
    bool eat(char c) { ws(); if (pos < text.size() && text[pos] == c) { ++pos; return true; } return false; }
    bool string(std::string& out) {
        if (!eat('"')) return false;
        out.clear();
        while (pos < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[pos++]);
            if (c == '"') return true;
            if (c < 32) return false;
            if (c == '\\') {
                if (pos == text.size()) return false;
                const char e = text[pos++];
                if (e == '"' || e == '\\' || e == '/') out += e;
                else if (e == 'b') out += '\b'; else if (e == 'f') out += '\f';
                else if (e == 'n') out += '\n'; else if (e == 'r') out += '\r'; else if (e == 't') out += '\t';
                else if (e == 'u') {
                    // Status keys/kept values are ASCII. Parse escapes strictly.
                    if (text.size() - pos < 4) return false;
                    unsigned value{}; const auto p = std::from_chars(text.data() + pos, text.data() + pos + 4, value, 16);
                    if (p.ec != std::errc{} || p.ptr != text.data() + pos + 4) return false;
                    pos += 4; out += value <= 127 ? static_cast<char>(value) : '?';
                } else return false;
            } else out += static_cast<char>(c);
            if (out.size() > 4096) return false;
        }
        return false;
    }
    bool null() { ws(); if (text.substr(pos, 4) != "null") return false; pos += 4; return true; }
    bool value(std::string& out) { ws(); return pos < text.size() && text[pos] == '"' ? string(out) : null(); }
    bool done() { ws(); return pos == text.size(); }
private:
    std::string_view text; std::size_t pos{};
};
bool timestamp(std::string_view s) {
    if (s.empty()) return true;
    if ((s.size() != 20 && (s.size() < 22 || s.size() > 30 || s[19] != '.')) || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s.back() != 'Z') return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == s.size() - 1 || (i == 19 && s[i] == '.')) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}
} // namespace

ServerSelection select_server(Provider provider, std::string_view origin) {
    ServerSelection result;
    const char* cloud = provider == Provider::BitwardenUS ? "https://vault.bitwarden.com" : provider == Provider::BitwardenEU ? "https://vault.bitwarden.eu" : nullptr;
    if (provider != Provider::BitwardenUS && provider != Provider::BitwardenEU && provider != Provider::Vaultwarden) { result.error = "Unknown vault provider."; return result; }
    std::string normalized;
    if (!normalize_origin(origin.empty() && cloud ? cloud : origin, normalized)) { result.error = "Use an HTTPS server origin without credentials, query, fragment, or path."; return result; }
    if (cloud && normalized != cloud) { result.error = "The selected cloud provider requires its exact official origin."; return result; }
    result.ok = true; result.server = {provider, std::move(normalized)}; return result;
}
StatusResult parse_status(std::string_view json) {
    StatusResult result;
    result.error = "The CLI returned an invalid or unsupported status object.";
    if (json.size() > max_status_bytes) return result;
    JsonReader reader(json); if (!reader.eat('{')) return result;
    std::set<std::string> keys;
    std::string state, server, sync;
    bool first = true;
    while (!reader.eat('}')) {
        if (!first && !reader.eat(',')) return result;
        first = false;
        std::string key, value;
        if (!reader.string(key) || !keys.insert(key).second || !reader.eat(':')) return result;
        if (!reader.value(value)) return result;
        if (key == "status") state = value; else if (key == "serverUrl") server = value; else if (key == "lastSync") sync = value;
    }
    if (!reader.done() || !keys.contains("status") || !timestamp(sync)) return result;
    if (state == "unauthenticated") result.value.state = VaultState::Unauthenticated;
    else if (state == "locked") result.value.state = VaultState::Locked;
    else if (state == "unlocked") result.value.state = VaultState::Unlocked;
    else return result;
    // CLI represents the default US server as null/empty on some versions.
    if (server.empty()) server = "https://vault.bitwarden.com";
    if (!normalize_origin(server, result.value.server_origin)) return result;
    result.value.last_sync = std::move(sync); result.ok = true; result.error.clear(); return result;
}
std::string status_json(const Status& status) {
    const char* state = status.state == VaultState::Unauthenticated ? "unauthenticated" : status.state == VaultState::Locked ? "locked" : status.state == VaultState::Unlocked ? "unlocked" : "unknown";
    // Only validated state/origin/timestamp reach output; do not echo arbitrary strings.
    std::string origin;
    if (!normalize_origin(status.server_origin, origin) || !timestamp(status.last_sync)) return "{\"status\":\"unknown\"}";
    return std::string("{\"status\":\"") + state + "\",\"serverUrl\":\"" + origin + "\",\"lastSync\":\"" + status.last_sync + "\"}";
}
PlanResult make_plan(std::string_view profile, Action action, const ServerConfig& server, VaultState observed_state) {
    PlanResult result;
    if (profile.empty() || profile.size() > 64 || !ascii_alnum(profile.front())) { result.error = "Invalid vault profile."; return result; }
    for (char c : profile) if (!ascii_alnum(c) && c != '_' && c != '-') { result.error = "Invalid vault profile."; return result; }
    result.plan.profile = std::string(profile);
    switch (action) {
    case Action::Status: result.plan.arguments = {"status"}; break;
    case Action::Sync: result.plan.arguments = {"sync"}; break;
    case Action::Lock: result.plan.arguments = {"lock"}; break;
    case Action::Logout: result.plan.arguments = {"logout"}; break;
    case Action::ConfigureServer: {
        if (observed_state != VaultState::Unauthenticated) { result.error = "Log out in the official CLI before changing this profile's server."; return result; }
        const auto selected = select_server(server.provider, server.origin);
        if (!selected.ok) { result.error = selected.error; return result; }
        result.plan.arguments = {"config", "server", selected.server.origin}; break;
    }
    default: result.error = "Unsupported vault action."; return result;
    }
    result.plan.arguments.push_back("--nointeraction");
    result.ok = true; return result;
}
} // namespace causalis::vault
