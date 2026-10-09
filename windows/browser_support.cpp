#include "browser_support.hpp"
#include <winhttp.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace causalis::windows {
namespace {
std::wstring lower(std::wstring value) {
    for (auto& ch : value) ch = static_cast<wchar_t>(std::towlower(ch));
    return value;
}
void validate_url_chars(std::wstring_view value) {
    if (value.empty() || value.size() > 8192) throw std::runtime_error("URL length is invalid.");
    for (const auto ch : value)
        if (ch <= 32 || ch == 127 || ch == L'\\')
            throw std::runtime_error("URL contains whitespace, control characters or backslashes.");
}
struct InternetHandle {
    HINTERNET value{};
    explicit InternetHandle(HINTERNET handle) : value(handle) {
        if (!value) throw std::runtime_error("Windows HTTPS operation failed.");
    }
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};
std::wstring header(HINTERNET request, DWORD query) {
    DWORD size = 0;
    if (WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
                            nullptr, &size, WINHTTP_NO_HEADER_INDEX)) return {};
    if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size > 32768)
        throw std::runtime_error("The response headers are invalid or too large.");
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
                            value.data(), &size, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("The response headers cannot be read.");
    value.resize(size / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}
bool canceled(const std::atomic_bool& flag, std::stop_token stop) {
    return flag.load(std::memory_order_relaxed) || stop.stop_requested();
}
NetworkResult fetch(std::wstring url, const std::atomic_bool& cancel, std::stop_token stop) {
    const auto started = std::chrono::steady_clock::now();
    NetworkResult result;
    result.requested_url = url;
    try {
        InternetHandle session(WinHttpOpen(L"Causalis-Original-Engine/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000))
            throw std::runtime_error("HTTPS timeouts could not be configured.");
        for (unsigned hop = 0; hop <= 5; ++hop) {
            if (canceled(cancel, stop)) return result;
            if (std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
                throw std::runtime_error("The navigation exceeded its 30-second transfer budget.");
            const HttpsUrl parsed = parse_https_url(url);
            InternetHandle connection(WinHttpConnect(session.value, parsed.host.c_str(), parsed.port, 0));
            InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", parsed.path.c_str(), nullptr,
                                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                      WINHTTP_FLAG_SECURE));
            DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
            if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
                throw std::runtime_error("HTTPS privacy restrictions could not be enabled.");
            // TLS certificate validation remains at the Windows defaults. No
            // SECURITY_FLAG_IGNORE_* option or user-supplied authorization exists.
            if (!WinHttpSendRequest(request.value,
                    L"Accept: text/html, application/xhtml+xml\r\nAccept-Encoding: identity\r\n", DWORD(-1L),
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(request.value, nullptr))
                throw std::runtime_error("HTTPS failed. Check connectivity, certificate and the five-second timeout.");
            if (canceled(cancel, stop)) return result;
            DWORD status = 0, size = sizeof(status);
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
                throw std::runtime_error("The response status cannot be read.");
            result.status = status;
            if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                if (hop == 5) throw std::runtime_error("More than five redirects were requested.");
                const auto location = header(request.value, WINHTTP_QUERY_LOCATION);
                if (location.empty()) throw std::runtime_error("The redirect has no Location header.");
                url = resolve_https_url(parsed.canonical, location);
                result.redirects = hop + 1;
                continue;
            }
            if (status < 200 || status >= 300) throw std::runtime_error("The server returned a non-success HTTP status.");
            auto type = lower(header(request.value, WINHTTP_QUERY_CONTENT_TYPE));
            const auto semicolon = type.find(L';');
            auto mime = type.substr(0, semicolon);
            while (!mime.empty() && std::iswspace(mime.back())) mime.pop_back();
            if (mime != L"text/html" && mime != L"application/xhtml+xml")
                throw std::runtime_error("Only HTML documents with a matching Content-Type are accepted.");
            const auto charset = type.find(L"charset=");
            if (charset != std::wstring::npos) {
                auto encoding = type.substr(charset + 8);
                const auto end = encoding.find(L';');
                if (end != std::wstring::npos) encoding.resize(end);
                while (!encoding.empty() && std::iswspace(encoding.front())) encoding.erase(encoding.begin());
                while (!encoding.empty() && std::iswspace(encoding.back())) encoding.pop_back();
                if (encoding != L"utf-8" && encoding != L"utf8" && encoding != L"\"utf-8\"")
                    throw std::runtime_error("This engine accepts UTF-8 HTML only.");
            }
            const auto encoding = lower(header(request.value, WINHTTP_QUERY_CONTENT_ENCODING));
            if (!encoding.empty() && encoding != L"identity")
                throw std::runtime_error("Compressed responses are not supported by this prototype.");
            const auto length = header(request.value, WINHTTP_QUERY_CONTENT_LENGTH);
            if (!length.empty()) {
                std::size_t consumed = 0;
                const auto count = std::stoull(length, &consumed);
                if (consumed != length.size() || count > max_document_bytes)
                    throw std::runtime_error("HTML exceeds the 2 MiB document limit.");
            }
            char buffer[16384];
            for (;;) {
                if (canceled(cancel, stop)) return result;
                DWORD received = 0;
                if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &received))
                    throw std::runtime_error("HTML could not be downloaded completely.");
                if (!received) break;
                if (result.html.size() + received > max_document_bytes)
                    throw std::runtime_error("HTML exceeds the 2 MiB document limit.");
                result.html.append(buffer, received);
                if (std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
                    throw std::runtime_error("The document exceeded the 30-second total transfer budget.");
            }
            if (result.html.starts_with("\xEF\xBB\xBF")) result.html.erase(0, 3);
            if (result.html.find('\0') != std::string::npos)
                throw std::runtime_error("The HTML response contains binary data.");
            (void)to_wide(result.html);
            result.final_url = parsed.canonical;
            break;
        }
    } catch (const std::exception& error) {
        result.html.clear();
        try { result.error = to_wide(error.what()); }
        catch (...) { result.error = L"The HTTPS document could not be loaded."; }
    }
    result.elapsed_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
    return result;
}
} // namespace

std::wstring to_wide(std::string_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Text exceeds the Windows string length limit.");
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (!count) throw std::runtime_error("The document is not valid UTF-8.");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), count))
        throw std::runtime_error("UTF-8 conversion failed.");
    return result;
}
std::string to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Text exceeds the Windows string length limit.");
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("The Windows text is invalid Unicode.");
    std::string result(static_cast<std::size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            result.data(), count, nullptr, nullptr))
        throw std::runtime_error("UTF-8 conversion failed.");
    return result;
}
std::string read_local_html(const std::wstring& name) {
    if (name.starts_with(L"\\\\") || name.starts_with(L"//"))
        throw std::runtime_error("Remote UNC/device paths are not accepted as local documents.");
    const std::filesystem::path file(name);
    if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("The path is not a regular file.");
    const auto size = std::filesystem::file_size(file);
    if (size > max_document_bytes) throw std::runtime_error("The document exceeds 2 MiB.");
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("The document cannot be opened.");
    std::string content(static_cast<std::size_t>(size), '\0');
    if (size && !input.read(content.data(), static_cast<std::streamsize>(size)))
        throw std::runtime_error("The document could not be read completely.");
    if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("The document changed while reading.");
    if (content.starts_with("\xEF\xBB\xBF")) content.erase(0, 3);
    if (content.find('\0') != std::string::npos) throw std::runtime_error("Binary documents are unsupported.");
    (void)to_wide(content);
    return content;
}
std::wstring profile_directory(std::wstring_view name) {
    if (name != L"Privat" && name != L"Arbeit" && name != L"Homelab")
        throw std::runtime_error("Unknown workspace.");
    wchar_t buffer[32768];
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (!count || count >= std::size(buffer)) throw std::runtime_error("LOCALAPPDATA is unavailable.");
    auto directory = std::filesystem::path(buffer) / L"Causalis" / L"workspaces" / std::wstring(name);
    std::filesystem::create_directories(directory);
    return directory.wstring();
}
void write_text_atomic(const std::wstring& name, std::string_view content) {
    const std::filesystem::path destination(name);
    const auto temporary = destination.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    try {
        std::ofstream output(std::filesystem::path(temporary), std::ios::binary | std::ios::trunc);
        if (!output || !output.write(content.data(), static_cast<std::streamsize>(content.size())))
            throw std::runtime_error("The file cannot be written.");
        output.close();
        if (!output) throw std::runtime_error("The file cannot be closed completely.");
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("The completed file could not replace the destination.");
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}
std::string escape_html(std::string_view value) {
    std::string result;
    for (const char ch : value) {
        if (ch == '&') result += "&amp;";
        else if (ch == '<') result += "&lt;";
        else if (ch == '>') result += "&gt;";
        else if (ch == '"') result += "&quot;";
        else result += ch;
    }
    return result;
}
HttpsUrl parse_https_url(std::wstring_view value) {
    validate_url_chars(value);
    const std::wstring input(value);
    if (input.size() < 9 || lower(input.substr(0, 8)) != L"https://")
        throw std::runtime_error("Only explicit https:// URLs are accepted.");
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1L);
    parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1L);
    if (!WinHttpCrackUrl(input.c_str(), static_cast<DWORD>(input.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength ||
        parts.dwUserNameLength || parts.dwPasswordLength || !parts.nPort)
        throw std::runtime_error("Only explicit https:// URLs without usernames or passwords are accepted.");
    HttpsUrl result;
    result.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    result.host = lower(std::move(result.host));
    if (result.host.find_first_of(L"%@?#") != std::wstring::npos)
        throw std::runtime_error("The URL host contains unsupported characters.");
    const auto authority_end = input.find_first_of(L"/?#", input.find(L"://") + 3);
    const auto authority = input.substr(input.find(L"://") + 3, authority_end == std::wstring::npos ?
                                       std::wstring::npos : authority_end - input.find(L"://") - 3);
    if (authority.find(L'@') != std::wstring::npos)
        throw std::runtime_error("User information in HTTPS URLs is not accepted.");
    result.port = parts.nPort;
    if (parts.dwUrlPathLength) result.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (result.path.empty()) result.path = L"/";
    if (parts.dwExtraInfoLength) {
        std::wstring extra(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        const auto hash = extra.find(L'#');
        result.path += extra.substr(0, hash);
    }
    result.canonical = L"https://";
    if (result.host.find(L':') != std::wstring::npos && !result.host.starts_with(L"["))
        result.canonical += L"[" + result.host + L"]";
    else result.canonical += result.host;
    if (result.port != 443) result.canonical += L":" + std::to_wstring(result.port);
    result.canonical += result.path;
    return result;
}
std::wstring resolve_https_url(std::wstring_view base, std::wstring_view href) {
    validate_url_chars(href);
    const auto parsed = parse_https_url(base);
    std::wstring candidate;
    if (href.starts_with(L"//")) candidate = L"https:" + std::wstring(href);
    else if (href.find(L':') != std::wstring_view::npos &&
             href.find(L':') < href.find_first_of(L"/?#")) candidate = std::wstring(href);
    else {
        auto origin = parsed.canonical.substr(0, parsed.canonical.size() - parsed.path.size());
        auto path = parsed.path.substr(0, parsed.path.find(L'?'));
        if (href.starts_with(L"/")) candidate = origin + std::wstring(href);
        else if (href.starts_with(L"?")) candidate = origin + path + std::wstring(href);
        else if (href.starts_with(L"#")) candidate = parsed.canonical;
        else candidate = origin + path.substr(0, path.rfind(L'/') + 1) + std::wstring(href);
    }
    auto target = parse_https_url(candidate);
    auto query = target.path.find(L'?');
    auto raw_path = target.path.substr(0, query);
    auto suffix = query == std::wstring::npos ? L"" : target.path.substr(query);
    std::vector<std::wstring> segments;
    std::size_t pos = 1;
    while (pos <= raw_path.size()) {
        auto end = raw_path.find(L'/', pos);
        if (end == std::wstring::npos) end = raw_path.size();
        auto part = raw_path.substr(pos, end - pos);
        if (part == L"..") { if (!segments.empty()) segments.pop_back(); }
        else if (part != L".") segments.push_back(std::move(part));
        if (end == raw_path.size()) break;
        pos = end + 1;
    }
    std::wstring normalized = L"/";
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i) normalized += L"/";
        normalized += segments[i];
    }
    if ((raw_path.ends_with(L"/.") || raw_path.ends_with(L"/..")) && !normalized.ends_with(L"/")) normalized += L"/";
    const auto origin = target.canonical.substr(0, target.canonical.size() - target.path.size());
    return origin + normalized + suffix;
}

struct NetworkLoader::Impl {
    struct Job {
        std::uint64_t tab_id{}, generation{};
        std::wstring url;
        std::shared_ptr<std::atomic_bool> canceled;
    };
    HWND target;
    std::mutex mutex;
    std::condition_variable_any ready;
    std::deque<Job> jobs;
    std::vector<NetworkResult> results;
    std::shared_ptr<std::atomic_bool> active_cancel;
    std::uint64_t active_tab{};
    bool alive{true};
    std::jthread worker;
    explicit Impl(HWND window) : target(window), worker([this](std::stop_token stop) { run(stop); }) {}
    ~Impl() {
        {
            std::lock_guard guard(mutex);
            alive = false;
            if (active_cancel) *active_cancel = true;
            for (auto& job : jobs) *job.canceled = true;
            jobs.clear();
        }
        worker.request_stop();
        ready.notify_all();
        worker.join();
    }
    void run(std::stop_token stop) {
        for (;;) {
            Job job;
            {
                std::unique_lock guard(mutex);
                ready.wait(guard, stop, [&] { return !jobs.empty() || !alive; });
                if (!alive || stop.stop_requested()) return;
                job = std::move(jobs.front());
                jobs.pop_front();
                active_tab = job.tab_id;
                active_cancel = job.canceled;
            }
            auto result = fetch(job.url, *job.canceled, stop);
            result.tab_id = job.tab_id;
            result.generation = job.generation;
            std::lock_guard guard(mutex);
            active_tab = 0;
            active_cancel.reset();
            if (!alive || canceled(*job.canceled, stop)) continue;
            results.push_back(std::move(result));
            // No heap pointer is posted. The queue owns every response, including
            // when the HWND is being destroyed; teardown holds this same mutex.
            PostMessageW(target, network_ready_message, 0, 0);
        }
    }
};
NetworkLoader::NetworkLoader(HWND target) : impl_(std::make_unique<Impl>(target)) {}
NetworkLoader::~NetworkLoader() = default;
void NetworkLoader::submit(std::uint64_t tab, std::uint64_t generation, std::wstring url) {
    std::lock_guard guard(impl_->mutex);
    if (!impl_->alive) return;
    if (impl_->active_tab == tab && impl_->active_cancel) *impl_->active_cancel = true;
    for (auto it = impl_->jobs.begin(); it != impl_->jobs.end();) {
        if (it->tab_id == tab) { *it->canceled = true; it = impl_->jobs.erase(it); }
        else ++it;
    }
    if (impl_->jobs.size() >= 12) throw std::runtime_error("The network queue is full.");
    impl_->jobs.push_back(Impl::Job{tab, generation, std::move(url), std::make_shared<std::atomic_bool>(false)});
    impl_->ready.notify_one();
}
void NetworkLoader::cancel(std::uint64_t tab) {
    std::lock_guard guard(impl_->mutex);
    if (impl_->active_tab == tab && impl_->active_cancel) *impl_->active_cancel = true;
    for (auto it = impl_->jobs.begin(); it != impl_->jobs.end();) {
        if (it->tab_id == tab) { *it->canceled = true; it = impl_->jobs.erase(it); }
        else ++it;
    }
}
void NetworkLoader::cancel_all() {
    std::lock_guard guard(impl_->mutex);
    if (impl_->active_cancel) *impl_->active_cancel = true;
    for (auto& job : impl_->jobs) *job.canceled = true;
    impl_->jobs.clear();
}
std::vector<NetworkResult> NetworkLoader::take_results() {
    std::lock_guard guard(impl_->mutex);
    std::vector<NetworkResult> result;
    result.swap(impl_->results);
    return result;
}
} // namespace causalis::windows
