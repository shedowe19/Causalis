#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include "causalis/engine.hpp"
#include "causalis/document.hpp"
#include "causalis/script.hpp"
#include "causalis/page.hpp"
#include "causalis/checkpoint.hpp"
#include "vault_bridge.hpp"
#include "browser_support.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <future>
#include <chrono>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
namespace support = causalis::windows;
constexpr int kToolbarHeight = 126;
constexpr int kStatusHeight = 28;
constexpr int kInspectorWidth = 330;
enum Command : UINT {
    kOpen = 100, kDemo, kDiagnostics, kAbout, kExit, kNew, kClose,
    kBack, kForward, kRefresh, kNavigate, kReading, kInspect, kFind,
    kFindNext, kSnapshot, kCompare, kBookmark, kBookmarks, kRunScript,
    kOnline, kProfile, kTabs, kAddress, kFindEdit, kSource, kExport,
    kWorkspaceSave, kWorkspaceOpen, kVaultChoose, kVaultStatus, kVaultLock,
    kVaultLaunch, kVaultSync, kVaultUS, kVaultEU, kVaultSelf, kCheckpointOpen
};
constexpr std::string_view kDemoHtml = R"HTML(<!doctype html><html><head>
<title>Causalis · Eigenständig</title><style>
body { background:#f5f7fc; color:#18263d; font-size:18px; }
h1 { color:#3757c4; font-size:40px; } h2 { color:#182c58; }
p { margin-bottom:16px; } a { color:#3757c4; }
</style></head><body><h1>Causalis</h1>
<p>Dein Browser-Labor mit einer eigenen HTML/CSS-Engine und einer eigenen kleinen JavaScript-Laufzeit.</p>
<h2>Projekte statt verlorener Tabs</h2><p>Privat, Arbeit und Homelab behalten ihre eigenen Tabs, ihren Verlauf und ihre Lesezeichen. Speichere ein Projekt, um die geöffneten Dokumente später wiederherzustellen.</p>
<h2>Seiten verstehen</h2><p>Aktiviere „Warum?“, dann klicke auf Text. Die Seiteninspektion erklärt Quelle, Position, Schrift und Linkziel des gezeichneten Elements. Die Leseansicht entfernt Skripte und aktive Ressourcen.</p>
<h2>Stand festhalten</h2><p>„Seitenstand“ speichert ein passives HTML-Dokument. „Vergleichen“ zeigt hinzugefügte und entfernte Textzeilen gegenüber einem gespeicherten Stand.</p>
<h2>Eigene Skript-Laufzeit</h2><p id="script-result">Dieser Text kann durch das lokale Beispielskript geändert werden.</p>
<script>console.log("Causalis eigene Laufzeit"); document.getElementById("script-result").textContent = "Das lokal gestartete Skript hat den Dokumenttext geändert.";</script>
<p>Wähle „Engine → Lokale Skripte einmal ausführen“. Skripte laufen nur nach deinem Klick, mit einem gemeinsamen Ausführungsbudget. Internetseiten bleiben ohne Skriptausführung.</p>
<h2>Bewusst experimentell</h2><p>Internet-Dokumente können über HTTPS gelesen werden, wenn du den Online-Versuch aktivierst. Keine Cookies, Formulare, Subressourcen, Passwortfelder oder Browser-Sandbox sind verfügbar. Verwende diesen Entwicklungsstand für Dokumente, nicht für Anmeldungen.</p>
<p><a href="https://example.com/">Ein kleines HTTPS-Testdokument öffnen</a></p>
<p>Bitwarden/Vaultwarden wird außerhalb der Seitenverarbeitung angebunden; dieser Entwicklungsstand verarbeitet keine Tresorgeheimnisse.</p>
</body></html>)HTML";
std::wstring utf8(std::string_view value) { return support::to_wide(value); }
std::string prefix(std::string_view value, std::size_t maximum) {
    if (value.size() <= maximum) return std::string(value);
    auto end = maximum;
    while (end && (static_cast<unsigned char>(value[end]) & 0xC0U) == 0x80U) --end;
    return std::string(value.substr(0, end));
}
int coordinate(float value) {
    if (!std::isfinite(value)) return 0;
    return static_cast<int>(std::lround(std::clamp(value, -2000000.0F, 2000000.0F)));
}

COLORREF rgb(causalis::Color value) {
    return RGB(value.r, value.g, value.b);
}

class FontCache {
public:
    FontCache() = default;
    FontCache(const FontCache&) = delete;
    FontCache& operator=(const FontCache&) = delete;
    ~FontCache() {
        for (const auto& entry : fonts_) DeleteObject(entry.second);
    }
    HFONT get(float size, bool bold) {
        const int pixels = std::isfinite(size)
            ? std::clamp(coordinate(size), 1, 256) : 16;
        const int key = pixels * 2 + (bold ? 1 : 0);
        const auto found = fonts_.find(key);
        if (found != fonts_.end()) return found->second;
        HFONT font = CreateFontW(-pixels, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!font) return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        try {
            fonts_.emplace(key, font);
        } catch (...) {
            DeleteObject(font);
            throw;
        }
        return font;
    }
private:
    std::map<int, HFONT> fonts_;
};

class SelectedObject {
public:
    SelectedObject(HDC dc, HGDIOBJ object) : dc_(dc), previous_(SelectObject(dc, object)) {}
    ~SelectedObject() {
        if (previous_ && previous_ != HGDI_ERROR) SelectObject(dc_, previous_);
    }
private:
    HDC dc_;
    HGDIOBJ previous_;
};

class SavedDC {
public:
    explicit SavedDC(HDC dc) : dc_(dc), saved_(SaveDC(dc)) {}
    ~SavedDC() { if (saved_) RestoreDC(dc_, saved_); }
    bool valid() const { return saved_ != 0; }
private:
    HDC dc_;
    int saved_;
};

void fill(HDC dc, RECT bounds, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (!brush) return;
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

void label(HDC dc, FontCache& fonts, RECT bounds, std::wstring_view value,
           COLORREF color, int size = 14, bool bold = false) {
    SelectedObject selection(dc, fonts.get(static_cast<float>(size), bold));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value.data(), static_cast<int>(value.size()), &bounds,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}


enum class PageKind { Builtin, Local, Https, Passive, Imported };
struct Page {
    std::string html;
    std::wstring location;
    PageKind kind{PageKind::Builtin};
    std::wstring network_summary;
};
struct Tab {
    std::uint64_t id{}, generation{};
    std::vector<Page> history;
    std::size_t history_index{};
    causalis::Frame frame;
    int scroll{};
    bool reading{}, loading{};
    std::wstring status;
    std::vector<std::string> console;
    unsigned inspected_source{};
    Page& page() { return history.at(history_index); }
    const Page& page() const { return history.at(history_index); }
};
struct Workspace {
    std::wstring name;
    std::vector<Tab> tabs;
    std::size_t selected{};
    std::vector<std::wstring> bookmarks;
    std::wstring bw_executable;
};
std::wstring control_text(HWND control) {
    const int count = GetWindowTextLengthW(control);
    if (count < 0 || count > 32768) throw std::runtime_error("Input exceeds the text limit.");
    std::wstring text(static_cast<std::size_t>(count) + 1, L'\0');
    const int actual = GetWindowTextW(control, text.data(), count + 1);
    text.resize(static_cast<std::size_t>(std::max(0, actual)));
    return text;
}
std::wstring lowercase(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}
std::wstring choose_file(HWND owner, bool save, const wchar_t* title, const wchar_t* filter,
                         std::wstring initial = {}, const wchar_t* extension = L"html") {
    wchar_t path[32768]{};
    if (initial.size() < std::size(path)) std::copy(initial.begin(), initial.end(), path);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = extension;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return {};
    return path;
}

class Host {
public:
    HWND window{}, tab_strip{}, address{}, profiles{}, online{}, inspector{}, find_edit{};
    std::map<UINT, HWND> controls;
    FontCache fonts;
    std::vector<Workspace> workspaces{{L"Privat"}, {L"Arbeit"}, {L"Homelab"}};
    std::size_t active_workspace{};
    std::uint64_t next_tab_id{1};
    std::unique_ptr<support::NetworkLoader> loader;
    bool inspect_mode{}, online_enabled{}, hover_link{}, smoke_test{};
    int wheel_remainder{};
    std::wstring hover_status, find_text;
    std::size_t last_find{};
    struct VaultTask {
        std::size_t workspace_index{};
        std::wstring operation;
        std::future<causalis::vault::Result> result;
    };
    std::vector<VaultTask> vault_tasks;
    bool collecting_vault{};
    explicit Host(bool smoke = false) : smoke_test(smoke) {}

    Workspace& workspace() { return workspaces.at(active_workspace); }
    Tab& tab() { return workspace().tabs.at(workspace().selected); }
    const Tab& tab() const { return workspaces.at(active_workspace).tabs.at(workspaces.at(active_workspace).selected); }
    int content_width() const {
        RECT bounds{}; GetClientRect(window, &bounds);
        return std::max(1, static_cast<int>(bounds.right) - (inspect_mode ? kInspectorWidth : 0));
    }
    int content_height() const {
        RECT bounds{}; GetClientRect(window, &bounds);
        return std::max(0, static_cast<int>(bounds.bottom) - kToolbarHeight - kStatusHeight);
    }
    void report(const std::exception& error) {
        try { tab().status = utf8(error.what()); }
        catch (...) { tab().status = L"Der Vorgang konnte nicht abgeschlossen werden."; }
        InvalidateRect(window, nullptr, FALSE);
    }
    HWND control(UINT id, const wchar_t* cls, const wchar_t* text, DWORD style) {
        HWND item = CreateWindowExW(cls == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
            cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 24,
            window, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), nullptr, nullptr);
        if (!item) throw std::runtime_error("A native UI control could not be created.");
        SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        controls.emplace(id, item);
        return item;
    }
    void initialize() {
        profiles = control(kProfile, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL);
        for (const auto& item : workspaces) SendMessageW(profiles, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.name.c_str()));
        SendMessageW(profiles, CB_SETCURSEL, 0, 0);
        online = control(kOnline, L"BUTTON", L"HTTPS-Versuch", BS_AUTOCHECKBOX);
        control(kOpen, L"BUTTON", L"Öffnen", BS_PUSHBUTTON);
        control(kSnapshot, L"BUTTON", L"Seitenstand", BS_PUSHBUTTON);
        control(kCompare, L"BUTTON", L"Vergleichen", BS_PUSHBUTTON);
        tab_strip = control(kTabs, WC_TABCONTROLW, L"", TCS_SINGLELINE | TCS_FOCUSNEVER);
        control(kNew, L"BUTTON", L"+", BS_PUSHBUTTON);
        control(kClose, L"BUTTON", L"×", BS_PUSHBUTTON);
        control(kBack, L"BUTTON", L"←", BS_PUSHBUTTON);
        control(kForward, L"BUTTON", L"→", BS_PUSHBUTTON);
        control(kRefresh, L"BUTTON", L"↻", BS_PUSHBUTTON);
        address = control(kAddress, L"EDIT", L"", ES_AUTOHSCROLL);
        SendMessageW(address, EM_SETLIMITTEXT, 8192, 0);
        control(kNavigate, L"BUTTON", L"Los", BS_PUSHBUTTON);
        control(kReading, L"BUTTON", L"Lesen", BS_PUSHBUTTON);
        control(kInspect, L"BUTTON", L"Warum?", BS_PUSHBUTTON);
        control(kBookmark, L"BUTTON", L"☆", BS_PUSHBUTTON);
        find_edit = control(kFindEdit, L"EDIT", L"", ES_AUTOHSCROLL);
        SendMessageW(find_edit, EM_SETLIMITTEXT, 256, 0);
        control(kFindNext, L"BUTTON", L"Suchen", BS_PUSHBUTTON);
        inspector = control(kSource, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
        ShowWindow(inspector, SW_HIDE);
        loader = std::make_unique<support::NetworkLoader>(window);
        for (std::size_t i = 0; i < workspaces.size(); ++i) {
            active_workspace = i;
            new_tab(false);
            if (!smoke_test) load_bookmarks();
        }
        active_workspace = 0;
        layout_controls();
        rebuild_tabs();
        reflow(true);
    }
    void layout_controls() {
        RECT bounds{}; GetClientRect(window, &bounds);
        const int width = std::max(780, static_cast<int>(bounds.right));
        auto move = [&](UINT id, int x, int y, int w, int h = 28) {
            MoveWindow(controls.at(id), x, y, w, h, TRUE);
        };
        move(kProfile, 126, 8, 105, 180);
        move(kOnline, 242, 8, 130);
        move(kOpen, 382, 8, 78);
        move(kSnapshot, 468, 8, 104);
        move(kCompare, 580, 8, 104);
        move(kFindEdit, width - 230, 44, 136);
        move(kFindNext, width - 87, 44, 78);
        move(kTabs, 8, 44, width - 326, 31);
        move(kNew, width - 310, 44, 32);
        move(kClose, width - 272, 44, 32);
        move(kBack, 8, 85, 32); move(kForward, 46, 85, 32); move(kRefresh, 84, 85, 32);
        move(kAddress, 126, 85, width - 399);
        move(kNavigate, width - 265, 85, 45);
        move(kReading, width - 212, 85, 64);
        move(kInspect, width - 140, 85, 80);
        move(kBookmark, width - 52, 85, 42);
        MoveWindow(inspector, static_cast<int>(bounds.right) - kInspectorWidth + 5,
                   kToolbarHeight + 7, kInspectorWidth - 12, std::max(0, content_height() - 14), TRUE);
    }
    void new_tab(bool update = true) {
        if (workspace().tabs.size() >= 8) throw std::runtime_error("Each workspace is limited to eight tabs.");
        Tab next;
        next.id = next_tab_id++;
        next.history.push_back(Page{std::string(kDemoHtml), L"causalis:start", PageKind::Builtin});
        workspace().tabs.push_back(std::move(next));
        workspace().selected = workspace().tabs.size() - 1;
        if (update) { rebuild_tabs(); reflow(true); }
    }
    void close_tab() {
        if (loader) loader->cancel(tab().id);
        workspace().tabs.erase(workspace().tabs.begin() + static_cast<std::ptrdiff_t>(workspace().selected));
        if (workspace().tabs.empty()) new_tab(false);
        workspace().selected = std::min(workspace().selected, workspace().tabs.size() - 1);
        rebuild_tabs(); reflow();
    }
    void rebuild_tabs() {
        TabCtrl_DeleteAllItems(tab_strip);
        for (std::size_t i = 0; i < workspace().tabs.size(); ++i) {
            auto& item = workspace().tabs[i];
            std::wstring title;
            try { title = utf8(item.frame.title); } catch (...) {}
            if (title.empty()) {
                if (item.page().kind == PageKind::Builtin) title = L"Causalis";
                else title = item.page().kind == PageKind::Https ? L"HTTPS-Dokument" : L"Lokales Dokument";
            }
            if (title.size() > 26) title = title.substr(0, 23) + L"…";
            if (item.loading) title = L"… " + title;
            TCITEMW entry{}; entry.mask = TCIF_TEXT; entry.pszText = title.data();
            TabCtrl_InsertItem(tab_strip, static_cast<int>(i), &entry);
        }
        TabCtrl_SetCurSel(tab_strip, static_cast<int>(workspace().selected));
        SetWindowTextW(address, tab().page().location.c_str());
        EnableWindow(controls.at(kBack), tab().history_index > 0);
        EnableWindow(controls.at(kForward), tab().history_index + 1 < tab().history.size());
        SetWindowTextW(controls.at(kReading), tab().reading ? L"Original" : L"Lesen");
    }
    void update_scrollbar() {
        const int total = std::isfinite(tab().frame.height)
            ? std::clamp(coordinate(std::ceil(tab().frame.height)), 0, 2000000) : 0;
        tab().scroll = std::clamp(tab().scroll, 0, std::max(0, total - content_height()));
        SCROLLINFO info{sizeof(info)};
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        info.nMin = 0; info.nMax = std::max(0, total - 1);
        info.nPage = static_cast<UINT>(content_height()); info.nPos = tab().scroll;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
    }
    void reflow(bool reset_scroll = false) {
        if (workspace().tabs.empty() || content_height() <= 0) return;
        HDC dc = GetDC(window);
        if (!dc) return;
        try {
            const std::string html = tab().reading ? causalis::Document(tab().page().html).reading_html() : tab().page().html;
            auto next = causalis::render(html, static_cast<float>(content_width()),
                [&](std::string_view text, float size, bool bold) -> float {
                    const auto wide = utf8(text);
                    SelectedObject selected(dc, fonts.get(size, bold));
                    SIZE measured{};
                    if (!GetTextExtentPoint32W(dc, wide.data(), static_cast<int>(wide.size()), &measured))
                        return static_cast<float>(wide.size()) * size * 0.6F;
                    return static_cast<float>(measured.cx);
                });
            tab().frame = std::move(next);
            if (reset_scroll) tab().scroll = 0;
            tab().inspected_source = 0;
            hover_link = false; hover_status.clear();
            auto title = utf8(tab().frame.title);
            if (title.empty()) title = L"Dokument";
            SetWindowTextW(window, (title + L" — Causalis · " + workspace().name).c_str());
        } catch (const std::exception& error) { report(error); }
        ReleaseDC(window, dc);
        update_scrollbar(); rebuild_tabs(); update_inspector();
        InvalidateRect(window, nullptr, FALSE);
    }
    void cancel_loading(Tab& item) {
        ++item.generation;
        item.loading = false;
        if (loader) loader->cancel(item.id);
    }
    void commit(Tab& item, Page next) {
        if (next.html.size() > support::max_document_bytes) throw std::runtime_error("The page exceeds 2 MiB.");
        if (item.history_index + 1 < item.history.size()) item.history.resize(item.history_index + 1);
        item.history.push_back(std::move(next));
        item.history_index = item.history.size() - 1;
        std::size_t bytes = 0;
        for (const auto& page : item.history) bytes += page.html.size();
        while (item.history.size() > 8 || (bytes > 8 * 1024 * 1024 && item.history.size() > 1)) {
            bytes -= item.history.front().html.size();
            item.history.erase(item.history.begin());
            --item.history_index;
        }
        item.frame = {};
        item.frame.title = causalis::Document(item.page().html).title();
        item.console.clear(); item.reading = false; item.scroll = 0;
        item.inspected_source = 0; item.status.clear();
    }
    void load_local(const std::wstring& path, PageKind kind = PageKind::Local) {
        auto html = support::read_local_html(path);
        cancel_loading(tab());
        commit(tab(), Page{std::move(html), std::filesystem::absolute(path).wstring(), kind});
        reflow(true);
    }
    void open_document() {
        const auto path = choose_file(window, false, L"Lokale UTF-8-HTML-Datei öffnen",
                                     L"HTML-Dateien\0*.html;*.htm\0Alle Dateien\0*.*\0\0");
        if (!path.empty()) load_local(path);
    }
    void navigate(std::wstring target, bool from_link = false) {
        if (target.empty()) return;
        if (target.starts_with(L"#")) {
            tab().status = L"Ankernavigation ist in dieser Engine noch nicht implementiert.";
            InvalidateRect(window, nullptr, FALSE); return;
        }
        if (target == L"causalis:start") {
            cancel_loading(tab());
            commit(tab(), Page{std::string(kDemoHtml), target, PageKind::Builtin});
            reflow(true); return;
        }
        const bool https = lowercase(target).starts_with(L"https://");
        if (https || (from_link && tab().page().kind == PageKind::Https)) {
            if (from_link && !https) target = support::resolve_https_url(tab().page().location, target);
            target = support::parse_https_url(target).canonical;
            if (!online_enabled) {
                tab().status = L"Aktiviere den HTTPS-Versuch, um ein Internet-Dokument zu lesen.";
                InvalidateRect(window, nullptr, FALSE); return;
            }
            cancel_loading(tab());
            loader->submit(tab().id, tab().generation, target);
            tab().loading = true;
            tab().status = L"HTTPS-Dokument wird geladen · keine Skripte oder Subressourcen";
            rebuild_tabs(); SetWindowTextW(address, target.c_str());
            InvalidateRect(window, nullptr, FALSE); return;
        }
        if (target.find(L"://") != std::wstring::npos || target.starts_with(L"javascript:") ||
            target.starts_with(L"data:") || target.starts_with(L"file:") || target.starts_with(L"mailto:"))
            throw std::runtime_error("This navigation scheme is not supported.");
        if (from_link) {
            if (tab().page().kind != PageKind::Local)
                throw std::runtime_error("Relative file links require a local document.");
            if (target.find(L'?') != std::wstring::npos || target.find(L'#') != std::wstring::npos ||
                target.find(L'%') != std::wstring::npos || target.find(L':') != std::wstring::npos ||
                target.starts_with(L"\\") || target.starts_with(L"/"))
                throw std::runtime_error("This local link is outside the supported relative-file subset.");
            const auto base = std::filesystem::path(tab().page().location).parent_path();
            target = (base / target).lexically_normal().wstring();
            const auto extension = lowercase(std::filesystem::path(target).extension().wstring());
            if (extension != L".html" && extension != L".htm")
                throw std::runtime_error("Local links open HTML files only.");
        }
        load_local(target);
    }
    void accept_network() {
        for (auto& result : loader->take_results()) {
            Tab* destination = nullptr;
            for (auto& group : workspaces) for (auto& item : group.tabs)
                if (item.id == result.tab_id) destination = &item;
            if (!destination || destination->generation != result.generation || !destination->loading) continue;
            destination->loading = false;
            if (!result.error.empty()) destination->status = result.error;
            else {
                const auto summary = L"HTTP " + std::to_wstring(result.status) + L" · " +
                    std::to_wstring(result.html.size()) + L" Byte · " + std::to_wstring(result.elapsed_ms) +
                    L" ms · " + std::to_wstring(result.redirects) + L" Weiterleitungen";
                commit(*destination, Page{std::move(result.html), std::move(result.final_url), PageKind::Https, summary});
                destination->status = summary;
            }
            if (destination == &tab()) reflow(true);
        }
        rebuild_tabs(); InvalidateRect(window, nullptr, FALSE);
    }
    void travel(int direction) {
        const auto index = static_cast<std::ptrdiff_t>(tab().history_index) + direction;
        if (index < 0 || index >= static_cast<std::ptrdiff_t>(tab().history.size())) return;
        cancel_loading(tab()); tab().history_index = static_cast<std::size_t>(index);
        tab().console.clear(); tab().reading = false; tab().status.clear(); reflow(true);
    }
    void refresh() {
        if (tab().loading) { cancel_loading(tab()); tab().status = L"Laden gestoppt."; rebuild_tabs(); return; }
        if (tab().page().kind == PageKind::Https) navigate(tab().page().location);
        else if (tab().page().kind == PageKind::Local) {
            auto html = support::read_local_html(tab().page().location);
            tab().page().html = std::move(html); tab().console.clear(); reflow();
        } else if (tab().page().kind == PageKind::Passive) {
            auto checkpoint = causalis::decode_checkpoint(read_project_file(tab().page().location, 4 * 1024 * 1024));
            tab().page().html = std::move(checkpoint.passive_html); reflow();
        } else reflow();
    }
    void set_online() {
        const bool checked = SendMessageW(online, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (checked && !online_enabled) {
            const int accepted = MessageBoxW(window,
                L"Dieser Versuch lädt HTTPS-HTML mit der eigenen Engine. Skripte, Cookies, Formulare und Subressourcen bleiben aus.\n\n"
                L"Der Renderer läuft noch ohne Prozess-Sandbox. Verwende nur Dokumente, denen du vertraust, und keine Anmeldungen oder echten Passwörter.\n\n"
                L"Den HTTPS-Versuch für diese Sitzung aktivieren?",
                L"Causalis · Experimenteller Dokumentzugriff", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2);
            online_enabled = accepted == IDYES;
            SendMessageW(online, BM_SETCHECK, online_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        } else online_enabled = checked;
        if (!online_enabled) {
            loader->cancel_all();
            for (auto& group : workspaces) for (auto& item : group.tabs) {
                if (item.loading) item.status = L"HTTPS-Versuch beendet · ausstehendes Ergebnis verworfen.";
                ++item.generation; item.loading = false;
            }
        }
        rebuild_tabs();
    }
    void update_inspector() {
        if (!inspect_mode) return;
        std::wstring text = L"WARUM DIESE SEITE?\r\n\r\nArbeitsbereich: " + workspace().name + L"\r\nQuelle: " + tab().page().location;
        text += L"\r\nModus: ";
        text += tab().reading ? L"Passive Leseansicht" : L"Originale eigene Darstellung";
        text += L"\r\nDOM/Rendering: eigene C++-Engine\r\nWeb-Cookies/Formulare: nicht implementiert\r\nJavaScript: nur explizit lokal\r\n";
        if (!tab().page().network_summary.empty()) text += L"\r\n" + tab().page().network_summary + L"\r\n";
        if (tab().inspected_source) {
            text += L"\r\nGEWÄHLTE QUELLE " + std::to_wstring(tab().inspected_source) + L"\r\n";
            std::size_t shown = 0;
            for (const auto& item : tab().frame.display_list) {
                if (item.source_id != tab().inspected_source) continue;
                if (++shown > 6) { text += L"Weitere Zeichenbefehle…\r\n"; break; }
                text += item.kind == causalis::PaintKind::Text ? L"Text: " : L"Fläche: ";
                text += utf8(prefix(item.text, 240)) + L"\r\n";
                text += L"x=" + std::to_wstring(coordinate(item.bounds.x)) + L" y=" + std::to_wstring(coordinate(item.bounds.y)) +
                        L" w=" + std::to_wstring(coordinate(item.bounds.width)) + L" h=" + std::to_wstring(coordinate(item.bounds.height)) + L"\r\n";
                if (item.kind == causalis::PaintKind::Text) text += L"Schrift " + std::to_wstring(coordinate(item.font_size)) + L" px" + (item.bold ? L", fett" : L"") + L"\r\n";
                if (!item.link.empty()) text += L"Link: " + utf8(prefix(item.link, 400)) + L"\r\n";
            }
        } else text += L"\r\nKlicke Text oder eine Fläche, um die Zeichenquelle zu prüfen. In diesem Modus öffnen Klicks keine Links.\r\n";
        text += L"\r\nENGINE-HINWEISE\r\n";
        for (std::size_t i = 0; i < std::min<std::size_t>(12, tab().frame.diagnostics.size()); ++i)
            text += utf8(tab().frame.diagnostics[i].category + ": " + prefix(tab().frame.diagnostics[i].message, 500)) + L"\r\n";
        text += L"\r\nKONSOLE\r\n";
        for (const auto& line : tab().console) text += utf8(prefix(line, 500)) + L"\r\n";
        SetWindowTextW(inspector, text.c_str());
    }
    void toggle_inspector() {
        inspect_mode = !inspect_mode;
        ShowWindow(inspector, inspect_mode ? SW_SHOW : SW_HIDE);
        layout_controls(); reflow();
    }
    void run_scripts() {
        if (tab().page().kind != PageKind::Local && tab().page().kind != PageKind::Builtin)
            throw std::runtime_error("Only explicitly opened local/example documents may execute the small script subset. Imported projects remain passive.");
        causalis::PageOptions options;
        options.source = causalis::DocumentSource::Local;
        options.run_scripts = true;
        options.instruction_budget = 100000;
        auto result = causalis::render_page(tab().page().html, static_cast<float>(content_width()), options);
        tab().page().html = std::move(result.document_html);
        tab().console = std::move(result.console);
        for (auto& diagnostic : result.script_diagnostics) tab().console.push_back(std::move(diagnostic));
        tab().status = L"Lokale Ausführung abgeschlossen · gemeinsame Schrittgrenze 100000 · Details unter Warum?";
        reflow();
    }
    void find_next() {
        const auto entered = control_text(find_edit);
        if (entered.empty()) { SetFocus(find_edit); return; }
        if (entered != find_text) { find_text = entered; last_find = 0; }
        const auto needle = lowercase(find_text);
        const auto& commands = tab().frame.display_list;
        for (std::size_t step = 0; step < commands.size(); ++step) {
            const auto index = (last_find + step) % commands.size();
            if (commands[index].kind != causalis::PaintKind::Text) continue;
            const auto text = lowercase(utf8(commands[index].text));
            if (text.find(needle) != std::wstring::npos) {
                last_find = index + 1; tab().scroll = coordinate(commands[index].bounds.y) - 20;
                tab().status = L"Treffer: " + utf8(prefix(commands[index].text, 160));
                update_scrollbar(); InvalidateRect(window, nullptr, FALSE); return;
            }
        }
        tab().status = L"Kein Treffer im gezeichneten Text. Suche arbeitet innerhalb einzelner Textläufe.";
        InvalidateRect(window, nullptr, FALSE);
    }
    void snapshot() {
        const auto directory = support::profile_directory(workspace().name);
        const auto initial = (std::filesystem::path(directory) / L"Seitenstand.causalis").wstring();
        const auto path = choose_file(window, true, L"Passiven Seitenstand speichern", L"Causalis Seitenstand\0*.causalis\0\0", initial, L"causalis");
        if (path.empty()) return;
        const auto checkpoint = causalis::make_checkpoint(causalis::Document(tab().page().html), support::to_utf8(tab().page().location));
        support::write_text_atomic(path, causalis::encode_checkpoint(checkpoint));
        tab().status = L"Passiver Seitenstand gespeichert: " + path;
        InvalidateRect(window, nullptr, FALSE);
    }
    std::string read_project_file(const std::wstring& path, std::uintmax_t maximum) {
        if (path.starts_with(L"\\\\") || path.starts_with(L"//")) throw std::runtime_error("Remote/device files are not accepted.");
        const auto file = std::filesystem::path(path);
        if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("The file is not a regular file.");
        const auto size = std::filesystem::file_size(file);
        if (size > maximum) throw std::runtime_error("The project/checkpoint file exceeds its limit.");
        std::ifstream input(file, std::ios::binary);
        if (!input) throw std::runtime_error("The file cannot be opened.");
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (size && !input.read(bytes.data(), static_cast<std::streamsize>(size)))
            throw std::runtime_error("The file could not be read completely.");
        if (input.peek() != std::char_traits<char>::eof() || input.bad())
            throw std::runtime_error("The file changed or failed while being read.");
        return bytes;
    }
    void open_checkpoint() {
        const auto path = choose_file(window, false, L"Passiven Seitenstand öffnen", L"Causalis Seitenstand\0*.causalis\0\0", {}, L"causalis");
        if (path.empty()) return;
        auto checkpoint = causalis::decode_checkpoint(read_project_file(path, 4 * 1024 * 1024));
        cancel_loading(tab());
        commit(tab(), Page{std::move(checkpoint.passive_html), path, PageKind::Passive});
        reflow(true);
    }
    void compare() {
        const auto path = choose_file(window, false, L"Seitenstand zum Textvergleich öffnen", L"Causalis Seitenstand\0*.causalis\0\0", {}, L"causalis");
        if (path.empty()) return;
        const auto previous = causalis::decode_checkpoint(read_project_file(path, 4 * 1024 * 1024));
        const auto current = causalis::make_checkpoint(causalis::Document(tab().page().html), support::to_utf8(tab().page().location));
        const auto changes = causalis::compare_checkpoints(previous, current);
        std::size_t removed = 0, added = 0;
        std::string details;
        for (const auto& change : changes) {
            const bool addition = change.kind == causalis::CheckpointChange::Kind::Added;
            const bool removal = change.kind == causalis::CheckpointChange::Kind::Removed;
            if (addition) ++added;
            if (removal) ++removed;
            if (details.size() < 18000) details += std::string(addition ? "+ " : removal ? "− " : "Hinweis: ") + prefix(change.text, 500) + "\n";
        }
        const auto text = L"Textzeilen hinzugefügt: " + std::to_wstring(added) + L"\nTextzeilen entfernt: " + std::to_wstring(removed) +
            L"\n\nPassiver Textvergleich; kein vollständiger struktureller DOM-Diff.\n\n" + utf8(details);
        MessageBoxW(window, text.c_str(), L"Causalis · Seitenvergleich", MB_OK);
    }
    void load_bookmarks() {
        try {
            const auto path = std::filesystem::path(support::profile_directory(workspace().name)) / L"bookmarks.txt";
            if (!std::filesystem::exists(path)) return;
            if (std::filesystem::file_size(path) > 256 * 1024) return;
            std::ifstream input(path, std::ios::binary);
            std::string line;
            while (workspace().bookmarks.size() < 100 && std::getline(input, line)) {
                if (line.size() <= 8192 && line.find('\0') == std::string::npos) workspace().bookmarks.push_back(utf8(line));
            }
        } catch (...) { /* A malformed optional bookmark store cannot prevent startup. */ }
    }
    void bookmark() {
        const auto location = tab().page().location;
        if (location.find_first_of(L"\r\n") != std::wstring::npos) throw std::runtime_error("This location cannot be bookmarked.");
        auto& bookmarks = workspace().bookmarks;
        if (std::find(bookmarks.begin(), bookmarks.end(), location) == bookmarks.end()) {
            if (bookmarks.size() >= 100) throw std::runtime_error("Each workspace is limited to 100 bookmarks.");
            bookmarks.push_back(location);
        }
        std::string content;
        for (const auto& item : bookmarks) content += support::to_utf8(item) + '\n';
        support::write_text_atomic((std::filesystem::path(support::profile_directory(workspace().name)) / L"bookmarks.txt").wstring(), content);
        tab().status = L"Lesezeichen in " + workspace().name + L" gespeichert.";
        InvalidateRect(window, nullptr, FALSE);
    }
    void show_bookmarks() {
        HMENU menu = CreatePopupMenu();
        for (std::size_t i = 0; i < workspace().bookmarks.size(); ++i) {
            auto label = workspace().bookmarks[i];
            if (label.size() > 120) label = label.substr(0, 117) + L"…";
            AppendMenuW(menu, MF_STRING, 1000 + i, label.c_str());
        }
        if (workspace().bookmarks.empty()) AppendMenuW(menu, MF_STRING | MF_GRAYED, 999, L"Noch keine Lesezeichen");
        POINT point{}; GetCursorPos(&point);
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (selected >= 1000 && selected < 1000 + workspace().bookmarks.size())
            navigate(workspace().bookmarks[selected - 1000]);
    }
    void save_workspace() {
        const auto initial = (std::filesystem::path(support::profile_directory(workspace().name)) / L"Projekt.causalis-project").wstring();
        const auto path = choose_file(window, true, L"Aktuelle Dokument-Tabs als Projekt speichern",
                                     L"Causalis Projekt\0*.causalis-project\0\0", initial, L"causalis-project");
        if (path.empty()) return;
        std::string bytes = "CAUSALIS-PROJECT-1\n" + std::to_string(workspace().tabs.size()) + " " +
                            std::to_string(workspace().selected) + "\n";
        for (const auto& item : workspace().tabs) {
            const auto location = support::to_utf8(item.page().location);
            bytes += std::to_string(static_cast<unsigned>(item.page().kind)) + " " + std::to_string(location.size()) + " " +
                     std::to_string(item.page().html.size()) + "\n";
            bytes += location; bytes += item.page().html; bytes += '\n';
            if (bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("Workspace export exceeds 16 MiB.");
        }
        support::write_text_atomic(path, bytes);
        tab().status = L"Originale Dokumentquellen gespeichert: " + path;
        InvalidateRect(window, nullptr, FALSE);
    }
    void open_workspace() {
        const auto path = choose_file(window, false, L"Gespeichertes Dokument-Projekt öffnen",
                                     L"Causalis Projekt\0*.causalis-project\0\0", {}, L"causalis-project");
        if (path.empty()) return;
        const auto bytes = read_project_file(path, 16 * 1024 * 1024);
        std::istringstream input(bytes);
        std::string magic;
        std::getline(input, magic);
        if (magic != "CAUSALIS-PROJECT-1") throw std::runtime_error("Unsupported workspace format.");
        std::size_t count = 0, selected = 0;
        if (!(input >> count >> selected) || count < 1 || count > 8 || selected >= count || input.get() != '\n')
            throw std::runtime_error("Invalid workspace header.");
        std::vector<Tab> imported;
        for (std::size_t i = 0; i < count; ++i) {
            unsigned kind = 0;
            std::size_t location_size = 0, html_size = 0;
            if (!(input >> kind >> location_size >> html_size) || kind > static_cast<unsigned>(PageKind::Imported) ||
                location_size > 32768 || html_size > support::max_document_bytes || input.get() != '\n')
                throw std::runtime_error("Invalid workspace document header.");
            std::string location(location_size, '\0'), html(html_size, '\0');
            if (!input.read(location.data(), static_cast<std::streamsize>(location_size)) ||
                !input.read(html.data(), static_cast<std::streamsize>(html_size)) || input.get() != '\n')
                throw std::runtime_error("Truncated workspace file.");
            if (location.find('\0') != std::string::npos || html.find('\0') != std::string::npos)
                throw std::runtime_error("Binary workspace fields are unsupported.");
            (void)utf8(html);
            auto wide_location = utf8(location);
            if (kind == static_cast<unsigned>(PageKind::Https)) (void)support::parse_https_url(wide_location);
            Tab item; item.id = next_tab_id++;
            item.history.push_back(Page{std::move(html), std::move(wide_location), PageKind::Imported});
            imported.push_back(std::move(item));
        }
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Unexpected trailing workspace data.");
        for (auto& item : workspace().tabs) cancel_loading(item);
        workspace().tabs = std::move(imported);
        workspace().selected = selected;
        tab().status = L"Projekt geöffnet · eingebettete Dokumente bleiben passiv, ohne Skriptausführung.";
        rebuild_tabs(); reflow(true);
    }
    void choose_vault_cli() {
        const auto path = choose_file(window, false, L"Offiziellen Bitwarden-CLI-Client bw.exe auswählen", L"Bitwarden CLI\0bw.exe\0\0", {}, L"exe");
        if (path.empty()) return;
        if (lowercase(std::filesystem::path(path).filename().wstring()) != L"bw.exe")
            throw std::runtime_error("Choose the official Bitwarden CLI executable named bw.exe.");
        workspace().bw_executable = std::filesystem::absolute(path).wstring();
        tab().status = L"Bitwarden CLI für " + workspace().name + L" ausgewählt. Keine Tresorentsperrung oder Passwortausgabe.";
        InvalidateRect(window, nullptr, FALSE);
    }
    void vault_action(UINT action) {
        if (workspace().bw_executable.empty()) {
            choose_vault_cli();
            if (workspace().bw_executable.empty()) return;
        }
        if (vault_tasks.size() >= 3) throw std::runtime_error("A maximum of three vault operations may run at once.");
        for (const auto& pending : vault_tasks)
            if (pending.workspace_index == active_workspace) throw std::runtime_error("A vault operation is already running in this workspace.");
        const auto executable = workspace().bw_executable;
        const auto directory = (std::filesystem::path(support::profile_directory(workspace().name)) / L"BitwardenCLI").wstring();
        std::wstring operation;
        causalis::vault::ServerConfig server;
        if (action == kVaultUS || action == kVaultEU || action == kVaultSelf) {
            const auto provider = action == kVaultUS ? causalis::vault::Provider::BitwardenUS :
                action == kVaultEU ? causalis::vault::Provider::BitwardenEU : causalis::vault::Provider::Vaultwarden;
            const auto selection = causalis::vault::select_server(provider, action == kVaultSelf ? support::to_utf8(control_text(address)) : "");
            if (!selection.ok) throw std::runtime_error(selection.error);
            server = selection.server;
            operation = L"Server konfigurieren";
        } else operation = action == kVaultStatus ? L"Status" : action == kVaultLock ? L"Sperren" : L"Synchronisieren";
        auto future = std::async(std::launch::async, [executable, directory, action, server] {
            if (action == kVaultStatus) return causalis::vault::run_status(executable, directory);
            if (action == kVaultLock) return causalis::vault::lock_vault(executable, directory);
            if (action == kVaultSync) return causalis::vault::sync_vault(executable, directory);
            return causalis::vault::configure_server(executable, directory, server);
        });
        vault_tasks.push_back(VaultTask{active_workspace, operation, std::move(future)});
        tab().status = L"Bitwarden: " + operation + L" läuft im separaten CLI-Prozess…";
        SetTimer(window, 1, 150, nullptr);
        InvalidateRect(window, nullptr, FALSE);
    }
    void collect_vault() {
        if (collecting_vault) return;
        collecting_vault = true;
        struct CollectGuard { bool& value; ~CollectGuard() { value = false; } } guard{collecting_vault};
        std::vector<std::pair<std::size_t, std::wstring>> notices;
        for (auto it = vault_tasks.begin(); it != vault_tasks.end();) {
            if (it->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
            std::wstring summary;
            try {
                const auto result = it->result.get();
                summary = L"Bitwarden · " + it->operation + L": " + utf8(result.ok ? result.output : result.error);
            } catch (...) { summary = L"Bitwarden CLI konnte den Vorgang nicht abschließen."; }
            const auto workspace_index = it->workspace_index;
            // Remove the consumed future before any modal UI starts a nested
            // Windows message loop and allows another WM_TIMER to be handled.
            it = vault_tasks.erase(it);
            auto& group = workspaces.at(workspace_index);
            if (!group.tabs.empty()) group.tabs[group.selected].status = summary;
            notices.emplace_back(workspace_index, std::move(summary));
        }
        if (vault_tasks.empty()) KillTimer(window, 1);
        for (const auto& [workspace_index, summary] : notices)
            if (workspace_index == active_workspace)
                MessageBoxW(window, summary.c_str(), L"Causalis · Tresor-Status", MB_OK);
        InvalidateRect(window, nullptr, FALSE);
    }
    void launch_vault() {
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", L"bitwarden:", nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) throw std::runtime_error("The Bitwarden desktop application is not registered for the bitwarden: protocol.");
        tab().status = L"Bitwarden-App geöffnet. Desktop- und getrennter CLI-Anmeldestatus sind unabhängig.";
        InvalidateRect(window, nullptr, FALSE);
    }
    void diagnostics() {
        if (!inspect_mode) toggle_inspector();
        update_inspector(); SetFocus(inspector);
    }
    const causalis::PaintCommand* command_at(int x, int y, bool links_only = false) const {
        if (x < 0 || x >= content_width() || y < kToolbarHeight || y >= kToolbarHeight + content_height()) return nullptr;
        const float document_y = static_cast<float>(y - kToolbarHeight + tab().scroll);
        for (auto it = tab().frame.display_list.rbegin(); it != tab().frame.display_list.rend(); ++it) {
            const auto& bounds = it->bounds;
            if ((!links_only || !it->link.empty()) && x >= bounds.x && x < bounds.x + bounds.width &&
                document_y >= bounds.y && document_y < bounds.y + bounds.height) return &*it;
        }
        return nullptr;
    }
    std::wstring link_at(int x, int y) const {
        const auto command = command_at(x, y, true);
        return command ? utf8(command->link) : L"";
    }
    void move_scroll(int next) {
        tab().scroll = next; hover_link = false; hover_status.clear();
        update_scrollbar(); InvalidateRect(window, nullptr, FALSE);
    }
    void paint_contents(HDC dc, RECT client) {
        fill(dc, client, RGB(255, 255, 255));
        const int bottom = std::max(kToolbarHeight, static_cast<int>(client.bottom) - kStatusHeight);
        {
            SavedDC saved(dc);
            if (saved.valid()) {
                IntersectClipRect(dc, 0, kToolbarHeight, content_width(), bottom);
                SetBkMode(dc, TRANSPARENT); SetTextAlign(dc, TA_LEFT | TA_TOP);
                for (const auto& command : tab().frame.display_list) {
                    const auto& bounds = command.bounds;
                    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.width) ||
                        !std::isfinite(bounds.height) || bounds.width < 0 || bounds.height < 0) continue;
                    const float y = bounds.y - static_cast<float>(tab().scroll) + kToolbarHeight;
                    if (!std::isfinite(y) || y + bounds.height < kToolbarHeight || y > bottom ||
                        bounds.x + bounds.width < 0 || bounds.x > content_width()) continue;
                    RECT rectangle{coordinate(bounds.x), coordinate(y), coordinate(bounds.x + bounds.width), coordinate(y + bounds.height)};
                    if (command.kind == causalis::PaintKind::FillRect) fill(dc, rectangle, rgb(command.color));
                    else if (command.kind == causalis::PaintKind::Text) {
                        if ((!find_text.empty() && lowercase(utf8(command.text)).find(lowercase(find_text)) != std::wstring::npos) ||
                            (inspect_mode && tab().inspected_source && tab().inspected_source == command.source_id))
                            fill(dc, rectangle, RGB(255, 235, 165));
                        const auto wide = utf8(command.text);
                        SelectedObject selection(dc, fonts.get(command.font_size, command.bold));
                        SetTextColor(dc, rgb(command.color));
                        TextOutW(dc, coordinate(bounds.x), coordinate(y), wide.data(), static_cast<int>(wide.size()));
                    }
                }
            }
        }
        fill(dc, RECT{0, 0, client.right, kToolbarHeight}, RGB(228, 234, 246));
        label(dc, fonts, RECT{14, 5, 120, 39}, L"Causalis", RGB(41, 61, 119), 20, true);
        if (inspect_mode) fill(dc, RECT{content_width(), kToolbarHeight, client.right, bottom}, RGB(233, 238, 247));
        fill(dc, RECT{0, bottom, client.right, client.bottom}, RGB(23, 35, 58));
        std::wstring line = !hover_status.empty() ? hover_status : tab().status;
        if (line.empty()) line = workspace().name + L" · eigene Engine · " +
            (tab().page().kind == PageKind::Https ? L"HTTPS-Dokument, keine Sandbox" : (tab().page().kind == PageKind::Imported ? L"Gespeichertes Projekt · passiv" : L"Dokumentmodus")) +
            L" · " + std::to_wstring(tab().frame.diagnostics.size()) + L" Hinweise";
        label(dc, fonts, RECT{10, bottom, client.right - 10, client.bottom}, line, RGB(237, 242, 255), 13);
    }
    void paint() {
        PAINTSTRUCT painting{};
        HDC dc = BeginPaint(window, &painting);
        if (!dc) { EndPaint(window, &painting); return; }
        RECT client{}; GetClientRect(window, &client);
        auto draw = [&](HDC target) {
            try { paint_contents(target, client); }
            catch (...) {
                fill(target, client, RGB(255, 255, 255));
                label(target, fonts, RECT{12, kToolbarHeight + 12, client.right - 12, kToolbarHeight + 60},
                      L"Die Zeichenliste konnte nicht vollständig dargestellt werden.", RGB(35, 35, 35));
            }
        };
        HDC memory = nullptr;
        HBITMAP bitmap = nullptr;
        if (client.right > 0 && client.bottom > 0 && static_cast<std::int64_t>(client.right) * client.bottom <= 16000000) {
            memory = CreateCompatibleDC(dc);
            if (memory) bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        }
        if (memory && bitmap) {
            { SelectedObject selection(memory, bitmap); draw(memory); BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY); }
        } else draw(dc);
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        EndPaint(window, &painting);
    }
    void native_smoke_test(std::vector<std::string>& checks) {
        auto require = [](bool passed, const char* failure) {
            if (!passed) throw std::runtime_error(failure);
        };
        require(smoke_test && !online_enabled && vault_tasks.empty(), "Smoke mode must start without network or vault operations.");
        require(workspaces.size() == 3 && workspace().tabs.size() == 1, "Initial workspace/tab state is invalid.");
        for (const auto& [id, item] : controls) {
            (void)id;
            require(IsWindow(item) != FALSE, "A native browser control was not constructed.");
        }
        require(TabCtrl_GetItemCount(tab_strip) == 1 && SendMessageW(profiles, CB_GETCOUNT, 0, 0) == 3,
                "Native tab/profile controls disagree with host state.");
        require(!tab().frame.display_list.empty() && tab().frame.title.find("Causalis") != std::string::npos,
                "The built-in document was not rendered by the original engine.");
        checks.push_back("PASS native window, controls, profiles and original built-in render");

        // Accelerators send HIWORD=1, whereas native buttons send HIWORD=0.
        // Exercise the real window procedure rather than invoking only methods.
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(kNew, 1), 0);
        require(workspace().tabs.size() == 2 && TabCtrl_GetItemCount(tab_strip) == 2,
                "Accelerator-style WM_COMMAND did not create a real tab.");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(kClose, 1), 0);
        require(workspace().tabs.size() == 1 && TabCtrl_GetItemCount(tab_strip) == 1,
                "Accelerator-style WM_COMMAND did not close the tab.");
        checks.push_back("PASS native accelerator command dispatch and tab creation/closure");

        wchar_t temporary_directory[MAX_PATH + 1]{};
        wchar_t temporary_file[MAX_PATH + 1]{};
        const DWORD count = GetTempPathW(static_cast<DWORD>(std::size(temporary_directory)), temporary_directory);
        require(count && count < std::size(temporary_directory), "The runner temporary directory is unavailable.");
        require(GetTempFileNameW(temporary_directory, L"Cau", 0, temporary_file) != 0,
                "A temporary local fixture could not be created.");
        struct TemporaryGuard {
            const wchar_t* path;
            ~TemporaryGuard() { DeleteFileW(path); }
        } temporary_guard{temporary_file};
        constexpr std::string_view fixture = R"HTML(<!doctype html><html><head><title>Windows-Smoke</title>
<style>body{font-size:18px;background:#f6f8fc;}h1{font-size:28px;color:#234899;}</style></head><body>
<h1>Grüße aus der eigenen Engine</h1><p id="smoke-result">Vorher: lokale UTF-8-Datei.</p>
<p>Unicode: Grüße, Österreich und 🚀.</p><script>console.log("native-smoke");
document.getElementById("smoke-result").textContent="Lokales Skript erfolgreich: Grüße 🚀.";</script>
</body></html>)HTML";
        support::write_text_atomic(temporary_file, fixture);
        load_local(temporary_file);
        require(tab().page().kind == PageKind::Local && tab().frame.title == "Windows-Smoke",
                "The native local loader did not render the fixture title.");
        require(causalis::Document(tab().page().html).plain_text().find("Grüße") != std::string::npos,
                "The local Windows UTF-8 loader lost document text.");
        const auto fixture_index = tab().history_index;
        require(fixture_index > 0, "Local navigation did not create history.");
        run_scripts();
        require(causalis::Document(tab().page().html).plain_text().find("Lokales Skript erfolgreich") != std::string::npos,
                "The original script runtime did not mutate the native document.");
        require(std::find(tab().console.begin(), tab().console.end(), "native-smoke") != tab().console.end(),
                "The original script runtime console did not reach the native host.");
        checks.push_back("PASS real temporary UTF-8 file load, native GDI text metrics and explicit own-script DOM mutation");

        SetWindowTextW(find_edit, L"erfolgreich");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(kFindNext, 1), 0);
        require(last_find > 0 && tab().status.starts_with(L"Treffer:"), "Native find did not locate the mutated text.");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(kReading, 1), 0);
        require(tab().reading && !tab().frame.display_list.empty(), "Reading mode did not reflow a passive document.");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(kInspect, 1), 0);
        require(inspect_mode && !control_text(inspector).empty(), "The native source inspector was not populated.");
        move_scroll(0);
        bool inspected = false;
        for (const auto& item : tab().frame.display_list) {
            if (item.kind != causalis::PaintKind::Text || item.source_id == 0 || item.bounds.width < 2 || item.bounds.height < 2) continue;
            const int x = coordinate(item.bounds.x + 1);
            const int y = coordinate(item.bounds.y + 1) + kToolbarHeight;
            if (x < 0 || x >= content_width() || y < kToolbarHeight || y >= kToolbarHeight + content_height()) continue;
            SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
            inspected = tab().inspected_source != 0;
            if (inspected) break;
        }
        require(inspected && control_text(inspector).find(L"GEWÄHLTE QUELLE") != std::wstring::npos,
                "A native content click did not select an inspectable paint source.");
        require(utf8(prefix("Grüße 🚀", 3)) == L"Gr", "UTF-8-safe diagnostic truncation split a code point.");
        checks.push_back("PASS native find, reading view, content hit testing, inspector and Unicode truncation");

        require(SetWindowPos(window, nullptr, 0, 0, 1060, 700, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "Native resize failed.");
        require(std::isfinite(tab().frame.width) && std::abs(tab().frame.width - static_cast<float>(content_width())) < 1.0F,
                "The original engine did not reflow after a native resize.");
        RECT client{};
        require(GetClientRect(window, &client) != FALSE && client.right > 100 && client.bottom > kToolbarHeight + kStatusHeight,
                "The smoke window has no drawable client area.");
        HDC dc = GetDC(window);
        require(dc != nullptr, "The smoke window GDI context is unavailable.");
        struct WindowDCGuard { HWND window; HDC dc; ~WindowDCGuard() { ReleaseDC(window, dc); } } window_dc{window, dc};
        {
            SelectedObject font(dc, fonts.get(18.0F, false));
            SIZE measured{};
            require(GetTextExtentPoint32W(dc, L"Grüße", 5, &measured) != FALSE && measured.cx > 0 && measured.cy > 0,
                    "Windows GDI did not measure the UTF-16 fixture text.");
        }
        HDC memory = CreateCompatibleDC(dc);
        require(memory != nullptr, "Offscreen GDI context creation failed.");
        struct MemoryDCGuard { HDC dc; ~MemoryDCGuard() { DeleteDC(dc); } } memory_dc{memory};
        HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        require(bitmap != nullptr, "Offscreen GDI bitmap creation failed.");
        struct BitmapGuard { HBITMAP bitmap; ~BitmapGuard() { DeleteObject(bitmap); } } bitmap_guard{bitmap};
        {
            SelectedObject selection(memory, bitmap);
            paint_contents(memory, client);
            require(GetPixel(memory, 2, 2) == RGB(228, 234, 246), "Native toolbar pixels were not painted.");
            require(GetPixel(memory, 2, client.bottom - 2) == RGB(23, 35, 58), "Native status pixels were not painted.");
        }
        // Also exercise BeginPaint/double-buffer dispatch on the real HWND.
        InvalidateRect(window, nullptr, FALSE);
        SendMessageW(window, WM_PAINT, 0, 0);
        checks.push_back("PASS native resize, original-engine reflow, offscreen GDI pixels and WM_PAINT dispatch");

        const auto local_source = tab().page().html;
        travel(-1);
        require(tab().history_index + 1 == fixture_index, "Native back navigation did not restore history.");
        travel(1);
        require(tab().history_index == fixture_index && tab().page().html == local_source,
                "Native forward navigation did not restore the mutated source snapshot.");
        const auto private_id = tab().id;
        SendMessageW(profiles, CB_SETCURSEL, 1, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(kProfile, CBN_SELCHANGE), reinterpret_cast<LPARAM>(profiles));
        require(active_workspace == 1 && tab().id != private_id && workspace().tabs.size() == 1,
                "Native workspace selection did not isolate document tabs.");
        SendMessageW(profiles, CB_SETCURSEL, 0, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(kProfile, CBN_SELCHANGE), reinterpret_cast<LPARAM>(profiles));
        require(active_workspace == 0 && tab().id == private_id && tab().page().html == local_source,
                "Switching back lost the original workspace document.");
        checks.push_back("PASS native back/forward snapshots and workspace control isolation");

        for (const auto kind : {PageKind::Https, PageKind::Passive, PageKind::Imported}) {
            tab().page().kind = kind;
            bool rejected = false;
            try { run_scripts(); } catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "Network/passive/imported content became eligible for local script execution.");
        }
        tab().page().kind = PageKind::Local;
        require(!online_enabled && vault_tasks.empty(), "Smoke validation unexpectedly enabled network or vault operations.");
        checks.push_back("PASS native script provenance gate and no-network/no-vault test contract");
    }

    void command(UINT id) {
        switch (id) {
        case kOpen: open_document(); break;
        case kDemo: navigate(L"causalis:start"); break;
        case kNew: new_tab(); break;
        case kClose: close_tab(); break;
        case kBack: travel(-1); break;
        case kForward: travel(1); break;
        case kRefresh: refresh(); break;
        case kNavigate: navigate(control_text(address)); break;
        case kReading: tab().reading = !tab().reading; reflow(true); break;
        case kInspect: toggle_inspector(); break;
        case kFind: SetFocus(find_edit); SendMessageW(find_edit, EM_SETSEL, 0, -1); break;
        case kFindNext: find_next(); break;
        case kSnapshot: snapshot(); break;
        case kCheckpointOpen: open_checkpoint(); break;
        case kCompare: compare(); break;
        case kBookmark: bookmark(); break;
        case kBookmarks: show_bookmarks(); break;
        case kRunScript: run_scripts(); break;
        case kOnline: set_online(); break;
        case kWorkspaceSave: save_workspace(); break;
        case kWorkspaceOpen: open_workspace(); break;
        case kVaultChoose: choose_vault_cli(); break;
        case kVaultStatus: case kVaultLock: case kVaultSync: case kVaultUS: case kVaultEU: case kVaultSelf: vault_action(id); break;
        case kVaultLaunch: launch_vault(); break;
        case kDiagnostics: diagnostics(); break;
        case kAbout:
            MessageBoxW(window, L"Causalis 0.2 · eigene C++20 HTML/CSS-Engine und begrenzte eigene JavaScript-Laufzeit.\n\n"
                L"Windows GDI zeichnet die originale Engine. Keine Chromium-, Gecko- oder WebView-Engine.\n\n"
                L"Noch kein Alltagsbrowser: kein vollständiger Webstandard, keine Prozess-Sandbox, keine Passwort-Autofill-Integration. "
                L"Der CLI-Tresoranschluss liest keine Geheimnisse. Windows-Ausführung muss in Windows geprüft werden.", L"Über Causalis", MB_OK);
            break;
        case kExit: DestroyWindow(window); break;
        }
    }
};

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Host* host = reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        host = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        host->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (!host) return DefWindowProcW(window, message, wparam, lparam);
    try {
        switch (message) {
        case WM_CREATE: host->initialize(); return 0;
        case WM_SIZE: host->layout_controls(); host->reflow(); return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = POINT{900, 440}; return 0;
        case WM_COMMAND:
            if (LOWORD(wparam) == kProfile && HIWORD(wparam) == CBN_SELCHANGE) {
                const LRESULT selection = SendMessageW(host->profiles, CB_GETCURSEL, 0, 0);
                if (selection >= 0 && static_cast<std::size_t>(selection) < host->workspaces.size()) {
                    host->active_workspace = static_cast<std::size_t>(selection);
                    host->find_text.clear(); host->last_find = 0;
                    host->rebuild_tabs(); host->reflow();
                }
            } else if (lparam == 0 || HIWORD(wparam) == BN_CLICKED) host->command(LOWORD(wparam));
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lparam)->idFrom == kTabs && reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE) {
                const auto selection = TabCtrl_GetCurSel(host->tab_strip);
                if (selection >= 0 && static_cast<std::size_t>(selection) < host->workspace().tabs.size()) {
                    host->workspace().selected = static_cast<std::size_t>(selection);
                    host->find_text.clear(); host->last_find = 0;
                    host->reflow();
                }
                return 0;
            }
            break;
        case support::network_ready_message: host->accept_network(); return 0;
        case WM_TIMER: if (wparam == 1) host->collect_vault(); return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{sizeof(info)}; info.fMask = SIF_ALL;
            GetScrollInfo(window, SB_VERT, &info);
            int next = host->tab().scroll;
            switch (LOWORD(wparam)) {
            case SB_LINEUP: next -= 32; break;
            case SB_LINEDOWN: next += 32; break;
            case SB_PAGEUP: next -= std::max(32, host->content_height() - 32); break;
            case SB_PAGEDOWN: next += std::max(32, host->content_height() - 32); break;
            case SB_THUMBPOSITION: case SB_THUMBTRACK: next = info.nTrackPos; break;
            case SB_TOP: next = 0; break;
            case SB_BOTTOM: next = info.nMax; break;
            default: return 0;
            }
            host->move_scroll(next); return 0;
        }
        case WM_MOUSEWHEEL: {
            host->wheel_remainder += GET_WHEEL_DELTA_WPARAM(wparam);
            if (host->wheel_remainder / WHEEL_DELTA) {
                UINT lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                const int step = lines == WHEEL_PAGESCROLL ? std::max(32, host->content_height() - 32) : static_cast<int>(std::min(lines, 100U)) * 24;
                host->move_scroll(host->tab().scroll - host->wheel_remainder / WHEEL_DELTA * step);
                host->wheel_remainder %= WHEEL_DELTA;
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wparam == VK_PRIOR) host->move_scroll(host->tab().scroll - host->content_height());
            else if (wparam == VK_NEXT) host->move_scroll(host->tab().scroll + host->content_height());
            else if (wparam == VK_HOME) host->move_scroll(0);
            else if (wparam == VK_END) host->move_scroll(2000000);
            else break;
            return 0;
        case WM_MOUSEMOVE: {
            const auto link = host->link_at(static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam)));
            host->hover_link = !link.empty() && !host->inspect_mode;
            if (host->hover_status != link) {
                host->hover_status = link;
                RECT bounds{}; GetClientRect(window, &bounds);
                bounds.top = std::max(kToolbarHeight, static_cast<int>(bounds.bottom) - kStatusHeight);
                InvalidateRect(window, &bounds, FALSE);
            }
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0}; TrackMouseEvent(&tracking); return 0;
        }
        case WM_MOUSELEAVE:
            host->hover_link = false; host->hover_status.clear(); InvalidateRect(window, nullptr, FALSE); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, host->hover_link ? IDC_HAND : IDC_ARROW)); return TRUE;
            }
            break;
        case WM_LBUTTONUP: {
            const int x = static_cast<short>(LOWORD(lparam)), y = static_cast<short>(HIWORD(lparam));
            if (host->inspect_mode) {
                const auto item = host->command_at(x, y);
                host->tab().inspected_source = item ? item->source_id : 0;
                host->update_inspector(); InvalidateRect(window, nullptr, FALSE);
            } else {
                const auto link = host->link_at(x, y);
                if (!link.empty()) host->navigate(link, true);
            }
            SetFocus(window); return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: host->paint(); return 0;
        case WM_DESTROY:
            KillTimer(window, 1);
            host->loader.reset();
            PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& error) {
        if (message == WM_CREATE) return -1;
        if (!host->workspace().tabs.empty()) host->report(error);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void menu_item(HMENU menu, UINT id, const wchar_t* label) { AppendMenuW(menu, MF_STRING, id, label); }
void smoke_report(std::string_view report, bool failure = false) {
    std::ofstream output("causalis-smoke-report.txt", std::ios::binary | std::ios::trunc);
    output.write(report.data(), static_cast<std::streamsize>(report.size()));
    output.close();
    const HANDLE stream = GetStdHandle(failure ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (stream && stream != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(stream, report.data(), static_cast<DWORD>(report.size()), &written, nullptr);
    }
    OutputDebugStringW(utf8(report).c_str());
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (!arguments) return 1;
    bool smoke = false;
    for (int i = 1; i < argument_count; ++i)
        if (std::wstring_view(arguments[i]) == L"--smoke-test") smoke = true;
    LocalFree(arguments);
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_TAB_CLASSES};
    if (!InitCommonControlsEx(&common)) {
        if (smoke) smoke_report("FAIL windows-native-smoke: InitCommonControlsEx failed.\n", true);
        return 1;
    }
    WNDCLASSEXW cls{sizeof(cls)};
    cls.style = CS_HREDRAW | CS_VREDRAW; cls.lpfnWndProc = window_proc;
    cls.hInstance = instance; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION); cls.lpszClassName = L"CausalisOriginalEngineHost";
    if (!RegisterClassExW(&cls)) {
        if (smoke) smoke_report("FAIL windows-native-smoke: RegisterClassExW failed.\n", true);
        return 1;
    }
    HMENU menu = CreateMenu(), file = CreatePopupMenu(), engine = CreatePopupMenu(), vault = CreatePopupMenu();
    menu_item(file, kNew, L"Neuer Tab\tStrg+T"); menu_item(file, kOpen, L"HTML öffnen…\tStrg+O");
    menu_item(file, kClose, L"Tab schließen\tStrg+W"); menu_item(file, kDemo, L"Startdokument");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    menu_item(file, kWorkspaceSave, L"Projekt speichern…\tStrg+Umschalt+S");
    menu_item(file, kWorkspaceOpen, L"Projekt öffnen…"); menu_item(file, kSnapshot, L"Passiven Seitenstand speichern…");
    menu_item(file, kCheckpointOpen, L"Seitenstand öffnen…"); menu_item(file, kCompare, L"Seitenstand vergleichen…");
    menu_item(file, kBookmarks, L"Lesezeichen"); menu_item(file, kExit, L"Beenden");
    menu_item(engine, kReading, L"Leseansicht wechseln"); menu_item(engine, kDiagnostics, L"Seiteninspektion und Diagnose");
    menu_item(engine, kRunScript, L"Lokale Skripte einmal ausführen"); menu_item(engine, kAbout, L"Über Causalis");
    menu_item(vault, kVaultChoose, L"Offiziellen bw.exe-Client auswählen…");
    menu_item(vault, kVaultStatus, L"Tresorstatus prüfen"); menu_item(vault, kVaultLock, L"CLI-Tresor sperren");
    menu_item(vault, kVaultSync, L"CLI-Tresor synchronisieren");
    AppendMenuW(vault, MF_SEPARATOR, 0, nullptr);
    menu_item(vault, kVaultUS, L"Server: Bitwarden USA"); menu_item(vault, kVaultEU, L"Server: Bitwarden Europa");
    menu_item(vault, kVaultSelf, L"Server: Vaultwarden-URL aus Adresszeile");
    menu_item(vault, kVaultLaunch, L"Bitwarden-Desktop-App öffnen");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"Datei");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(engine), L"Engine");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(vault), L"Tresor");
    Host host(smoke);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"Causalis · Eigene Engine", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 820, nullptr, menu, instance, &host);
    if (!window) {
        DestroyMenu(menu);
        if (smoke) smoke_report("FAIL windows-native-smoke: native window/control initialization failed.\n", true);
        return 1;
    }
    if (smoke) {
        std::vector<std::string> checks;
        int smoke_result = 0;
        std::string summary;
        try {
            host.native_smoke_test(checks);
            summary = "PASS windows-native-smoke\n";
        } catch (const std::exception& error) {
            smoke_result = 2;
            summary = "FAIL windows-native-smoke: " + std::string(error.what()) + "\n";
        } catch (...) {
            smoke_result = 2;
            summary = "FAIL windows-native-smoke: unexpected native exception.\n";
        }
        for (const auto& check : checks) summary += check + "\n";
        smoke_report(summary, smoke_result != 0);
        DestroyWindow(window);
        return smoke_result;
    }
    ACCEL keys[] = {
        {FVIRTKEY | FCONTROL, 'O', kOpen}, {FVIRTKEY | FCONTROL, 'T', kNew}, {FVIRTKEY | FCONTROL, 'W', kClose},
        {FVIRTKEY | FCONTROL, 'F', kFind}, {FVIRTKEY | FCONTROL | FSHIFT, 'S', kWorkspaceSave},
        {FVIRTKEY, VK_F5, kRefresh}, {FVIRTKEY | FALT, VK_LEFT, kBack}, {FVIRTKEY | FALT, VK_RIGHT, kForward}
    };
    HACCEL accelerators = CreateAcceleratorTableW(keys, static_cast<int>(std::size(keys)));
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    int result = 0;
    while ((result = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            const HWND focus = GetFocus();
            if (focus == host.address || focus == host.find_edit) {
                SendMessageW(window, WM_COMMAND, focus == host.address ? kNavigate : kFindNext, 0); continue;
            }
        }
        if (message.message == WM_KEYDOWN && message.wParam == 'L' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SetFocus(host.address); SendMessageW(host.address, EM_SETSEL, 0, -1); continue;
        }
        if (!accelerators || !TranslateAcceleratorW(window, accelerators, &message)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    if (accelerators) DestroyAcceleratorTable(accelerators);
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}
