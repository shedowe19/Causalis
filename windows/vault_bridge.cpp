#include "vault_bridge.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <mutex>
#include <utility>
#include <vector>

namespace causalis::vault {
namespace {
constexpr DWORD timeout_ms = 20000;
constexpr std::size_t capture_limit = 64 * 1024;
std::mutex bridge_mutex;
class Handle {
public:
    explicit Handle(HANDLE h = nullptr) : value_(h) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept { if (this != &other) { close(); value_ = std::exchange(other.value_, nullptr); } return *this; }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void close() { if (*this) CloseHandle(value_); value_ = nullptr; }
private:
    HANDLE value_{};
};
Result failure(const char* reason) { return {false, {}, reason}; }
bool same(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() && CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
std::wstring environment_value(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (!needed || needed > 32768) return {};
    std::wstring value(needed, '\0');
    const DWORD read = GetEnvironmentVariableW(name, value.data(), needed);
    if (!read || read >= needed) return {};
    value.resize(read); return value;
}
bool absolute_local_path(const std::wstring& value, std::wstring& full) {
    if (value.size() < 4 || value.size() > 30000 || value[1] != ':' || value[2] != '\\' ||
        !((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z'))) return false;
    const UINT drive = GetDriveTypeW(value.substr(0, 3).c_str());
    // A mapped network drive still uses a drive letter. Only explicitly local
    // storage types qualify; unknown/nonexistent/remote/optical roots fail closed.
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK) return false;
    for (std::size_t i = 0; i < value.size(); ++i) if (value[i] < 32 || value[i] == '"' || value[i] == '/' || value[i] == '<' || value[i] == '>' || value[i] == '*' || value[i] == '?' || value[i] == '|' || (value[i] == ':' && i != 1)) return false;
    std::wstring buffer(32768, '\0');
    const DWORD n = GetFullPathNameW(value.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!n || n >= buffer.size()) return false;
    buffer.resize(n);
    if (!same(value, buffer) || value.back() == '\\') return false;
    // Win32 trims trailing spaces/dots; prohibit alternate spellings and path aliases.
    std::size_t begin = 3;
    while (begin < value.size()) {
        auto end = value.find('\\', begin); if (end == std::wstring::npos) end = value.size();
        if (end == begin || value[end - 1] == ' ' || value[end - 1] == '.') return false;
        begin = end + 1;
    }
    full = std::move(buffer); return true;
}
bool ordinary_opened_path(HANDLE handle, const std::wstring& expected, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        static_cast<bool>(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory) return false;
    std::wstring final_path(32768, '\0');
    const DWORD n = GetFinalPathNameByHandleW(handle, final_path.data(), static_cast<DWORD>(final_path.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= final_path.size()) return false;
    final_path.resize(n);
    if (!final_path.starts_with(L"\\\\?\\")) return false;
    final_path.erase(0, 4);
    return same(final_path, expected);
}
bool pin_path(const std::wstring& path, bool directory, std::vector<Handle>& pinned) {
    std::size_t end = 3;
    while (end <= path.size()) {
        const bool current_directory = end != path.size() || directory;
        const auto component = path.substr(0, end);
        // Deny new write/delete handles while the browser checks and runs this
        // command. Checking attributes alone would leave a replacement race.
        // Directory sharing applies to the directory object, not child files:
        // the official CLI can still update files inside its dedicated directory.
        Handle handle(CreateFileW(component.c_str(), current_directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | (current_directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
        if (!handle || !ordinary_opened_path(handle.get(), component, current_directory)) return false;
        pinned.push_back(std::move(handle));
        if (end == path.size()) break;
        const auto next = path.find('\\', end + (end == 3 ? 0 : 1));
        end = next == std::wstring::npos ? path.size() : next;
    }
    return true;
}
bool prepare_profile(const std::wstring& exe, const std::wstring& appdata, std::string& profile, std::vector<Handle>& pinned) {
    std::wstring normalized_exe, normalized_data, local;
    if (!absolute_local_path(exe, normalized_exe) || !absolute_local_path(appdata, normalized_data) ||
        !absolute_local_path(environment_value(L"LOCALAPPDATA"), local)) return false;
    const auto separator = exe.find_last_of('\\');
    if (separator == std::wstring::npos || !same(std::wstring_view(exe).substr(separator + 1), L"bw.exe")) return false;
    if (!pin_path(exe, false, pinned)) return false;
    for (const auto* name : {L"Privat", L"Arbeit", L"Homelab"}) {
        const std::wstring expected = local + L"\\Causalis\\workspaces\\" + name + L"\\BitwardenCLI";
        if (same(appdata, expected)) {
            const auto parent = appdata.substr(0, appdata.find_last_of('\\'));
            if (!pin_path(parent, true, pinned)) return false;
            if (!CreateDirectoryW(appdata.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
            if (!pin_path(appdata, true, pinned)) return false;
            profile.assign(name, name + wcslen(name)); return true;
        }
    }
    return false;
}
bool is_unelevated() {
    HANDLE raw{}; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return false;
    Handle token(raw); TOKEN_ELEVATION elevation{}; DWORD read{};
    return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &read) && !elevation.TokenIsElevated;
}
std::vector<wchar_t> child_environment(const std::wstring& appdata) {
    // Whitelist rather than removing known secrets: no BW_SESSION, BW_CLIENTSECRET,
    // BW_PASSWORD, NODE_OPTIONS, proxy credentials, or arbitrary browser environment.
    std::vector<std::wstring> entries;
    for (const auto* name : {L"APPDATA", L"LOCALAPPDATA", L"SystemRoot", L"TEMP", L"TMP", L"USERPROFILE", L"WINDIR"}) {
        auto value = environment_value(name); if (!value.empty()) entries.emplace_back(std::wstring(name) + L"=" + value);
    }
    entries.emplace_back(L"BITWARDENCLI_APPDATA_DIR=" + appdata);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
    });
    std::vector<wchar_t> block;
    for (const auto& entry : entries) { block.insert(block.end(), entry.begin(), entry.end()); block.push_back('\0'); }
    block.push_back('\0'); return block;
}
std::wstring quote(std::wstring_view argument) {
    std::wstring result = L"\""; std::size_t slashes{};
    for (wchar_t c : argument) {
        if (c == '\\') { ++slashes; continue; }
        if (c == '"') { result.append(slashes * 2 + 1, '\\'); result += c; }
        else { result.append(slashes, '\\'); result += c; }
        slashes = 0;
    }
    result.append(slashes * 2, '\\'); result += '"'; return result;
}
bool create_pipe(Handle& read, Handle& write) {
    SECURITY_ATTRIBUTES attrs{sizeof(attrs), nullptr, TRUE}; HANDLE r{}, w{};
    if (!CreatePipe(&r, &w, &attrs, 0)) return false;
    read = Handle(r); write = Handle(w);
    return SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0) != 0;
}
struct Capture { bool ok{}; std::string output; std::string error; };
class Attributes {
public:
    Attributes() {
        SIZE_T size{}; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        storage_.resize(size); value_ = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(value_, 1, 0, &size)) value_ = nullptr;
    }
    ~Attributes() { if (value_) DeleteProcThreadAttributeList(value_); }
    PPROC_THREAD_ATTRIBUTE_LIST get() const { return value_; }
private:
    std::vector<unsigned char> storage_; PPROC_THREAD_ATTRIBUTE_LIST value_{};
};
bool drain(HANDLE pipe, std::string* output, std::size_t& total) {
    for (;;) {
        DWORD available{};
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return GetLastError() == ERROR_BROKEN_PIPE;
        if (!available) return true;
        std::array<char, 4096> buffer{}; DWORD n{};
        if (!ReadFile(pipe, buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &n, nullptr)) return GetLastError() == ERROR_BROKEN_PIPE;
        if (n > capture_limit - total) return false;
        total += n;
        if (output) output->append(buffer.data(), n);
        // Stderr is counted and discarded, never returned or written to logs.
    }
}
Capture launch(const std::wstring& exe, const std::wstring& appdata, const CommandPlan& plan) {
    Handle out_read, out_write, err_read, err_write;
    if (!create_pipe(out_read, out_write) || !create_pipe(err_read, err_write)) return {false, {}, "CLI capture setup failed."};
    SECURITY_ATTRIBUTES attrs{sizeof(attrs), nullptr, TRUE};
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attrs, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!input) return {false, {}, "CLI input setup failed."};
    Attributes attributes; if (!attributes.get()) return {false, {}, "CLI handle isolation failed."};
    std::array<HANDLE, 3> allowed{out_write.get(), err_write.get(), input.get()};
    if (!UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, allowed.data(), sizeof(allowed), nullptr, nullptr)) return {false, {}, "CLI handle isolation failed."};
    STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start);
    start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    start.StartupInfo.hStdOutput = out_write.get(); start.StartupInfo.hStdError = err_write.get(); start.StartupInfo.hStdInput = input.get();
    start.lpAttributeList = attributes.get();
    std::wstring command = quote(exe);
    for (const auto& arg : plan.arguments) { command += L" "; command += quote(std::wstring(arg.begin(), arg.end())); }
    auto environment = child_environment(appdata);
    std::array<wchar_t, 32768> system_dir{};
    const UINT len = GetSystemDirectoryW(system_dir.data(), static_cast<UINT>(system_dir.size()));
    if (!len || len >= system_dir.size()) return {false, {}, "System directory is unavailable."};
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
    job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &job_limits, sizeof(job_limits))) return {false, {}, "CLI lifetime isolation failed."};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
        environment.data(), system_dir.data(), &start.StartupInfo, &child)) return {false, {}, "The selected CLI could not be started."};
    Handle process(child.hProcess), thread(child.hThread);
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        TerminateProcess(process.get(), 1); WaitForSingleObject(process.get(), 1000);
        return {false, {}, "CLI lifetime isolation failed."};
    }
    out_write.close(); err_write.close(); input.close();
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) { TerminateJobObject(job.get(), 1); return {false, {}, "CLI startup failed."}; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string output; std::size_t total{};
    bool ended{};
    while (!ended) {
        if (std::chrono::steady_clock::now() >= deadline) { TerminateJobObject(job.get(), 1); WaitForSingleObject(process.get(), 1000); return {false, {}, "CLI operation timed out."}; }
        if (!drain(out_read.get(), &output, total) || !drain(err_read.get(), nullptr, total)) { TerminateJobObject(job.get(), 1); return {false, {}, "CLI output exceeded its bound or capture failed."}; }
        const DWORD waited = WaitForSingleObject(process.get(), 10);
        if (waited == WAIT_OBJECT_0) ended = true;
        else if (waited != WAIT_TIMEOUT) { TerminateJobObject(job.get(), 1); return {false, {}, "CLI process wait failed."}; }
    }
    if (!drain(out_read.get(), &output, total) || !drain(err_read.get(), nullptr, total)) return {false, {}, "CLI output exceeded its bound or capture failed."};
    DWORD code{}; if (!GetExitCodeProcess(process.get(), &code) || code != 0) return {false, {}, "The official CLI rejected the operation. Inspect it externally for details."};
    return {true, std::move(output), {}};
}
Result perform(const std::wstring& exe, const std::wstring& appdata, Action action, const ServerConfig& server = {}) {
    std::unique_lock<std::mutex> guard(bridge_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return failure("A vault operation is already running. Retry when it finishes.");
    if (!is_unelevated()) return failure("Run Causalis as a normal user to use the vault bridge.");
    std::string profile;
    // Handles remain alive across every launch/job below, including both
    // commands of a server change. A selected executable is never write/delete
    // shareable while it is being validated, launched, or used by this bridge.
    std::vector<Handle> pinned_paths;
    if (!prepare_profile(exe, appdata, profile, pinned_paths)) return failure("Select a trusted native bw.exe and use this workspace's dedicated local vault directory.");
    VaultState state = VaultState::Unknown;
    if (action == Action::ConfigureServer) {
        const auto status_plan = make_plan(profile, Action::Status);
        const auto status_capture = launch(exe, appdata, status_plan.plan);
        if (!status_capture.ok) return failure(status_capture.error.c_str());
        const auto parsed = parse_status(status_capture.output);
        if (!parsed.ok) return failure(parsed.error.c_str());
        state = parsed.value.state;
    }
    const auto planned = make_plan(profile, action, server, state);
    if (!planned.ok) return failure(planned.error.c_str());
    const auto captured = launch(exe, appdata, planned.plan);
    if (!captured.ok) return failure(captured.error.c_str());
    if (action == Action::Status) {
        const auto parsed = parse_status(captured.output);
        if (!parsed.ok) return failure(parsed.error.c_str());
        return {true, status_json(parsed.value), {}};
    }
    return {true, action == Action::ConfigureServer ? "Server configured. This does not authenticate or connect a vault." :
        action == Action::Lock ? "Vault lock command completed." : action == Action::Logout ? "Vault logout command completed." : "Encrypted vault sync command completed.", {}};
}
} // namespace
Result run_status(const std::wstring& exe, const std::wstring& appdata) { return perform(exe, appdata, Action::Status); }
Result configure_server(const std::wstring& exe, const std::wstring& appdata, const ServerConfig& server) { return perform(exe, appdata, Action::ConfigureServer, server); }
Result lock_vault(const std::wstring& exe, const std::wstring& appdata) { return perform(exe, appdata, Action::Lock); }
Result sync_vault(const std::wstring& exe, const std::wstring& appdata) { return perform(exe, appdata, Action::Sync); }
Result logout_vault(const std::wstring& exe, const std::wstring& appdata) { return perform(exe, appdata, Action::Logout); }
} // namespace causalis::vault
#endif
