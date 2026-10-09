#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace causalis::windows {
inline constexpr UINT network_ready_message = WM_APP + 19;
inline constexpr std::size_t max_document_bytes = 2 * 1024 * 1024;
std::wstring to_wide(std::string_view value);
std::string to_utf8(std::wstring_view value);
std::string read_local_html(const std::wstring& path);
std::wstring profile_directory(std::wstring_view name);
void write_text_atomic(const std::wstring& path, std::string_view content);
std::string escape_html(std::string_view value);

// This host performs only explicit HTTPS document loads. No cookies, credentials,
// subresources, script downloads or permissive TLS flags are enabled.
struct HttpsUrl {
    std::wstring canonical, host, path;
    std::uint16_t port{443};
};
HttpsUrl parse_https_url(std::wstring_view value);
std::wstring resolve_https_url(std::wstring_view base, std::wstring_view href);
struct NetworkResult {
    std::uint64_t tab_id{}, generation{};
    std::wstring requested_url, final_url, error;
    std::string html;
    unsigned status{}, redirects{};
    std::uint64_t elapsed_ms{};
};
class NetworkLoader {
public:
    explicit NetworkLoader(HWND target);
    ~NetworkLoader();
    NetworkLoader(const NetworkLoader&) = delete;
    NetworkLoader& operator=(const NetworkLoader&) = delete;
    void submit(std::uint64_t tab_id, std::uint64_t generation, std::wstring url);
    void cancel(std::uint64_t tab_id);
    void cancel_all();
    std::vector<NetworkResult> take_results();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace causalis::windows
