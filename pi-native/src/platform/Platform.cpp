#include "platform/Platform.h"

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

namespace it::platform {

#if defined(_WIN32)
namespace {
std::string knownFolder(REFKNOWNFOLDERID id) {
    PWSTR w = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &w))) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        out.resize(static_cast<size_t>(n > 0 ? n - 1 : 0));
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    }
    CoTaskMemFree(w);
    return out;
}

LRESULT CALLBACK previewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;  // GL paints everything
    return DefWindowProcW(h, msg, wp, lp);
}
}  // namespace

std::string configDir() { return knownFolder(FOLDERID_RoamingAppData) + "\\ImageTunnel"; }
std::string cacheDir() { return knownFolder(FOLDERID_LocalAppData) + "\\ImageTunnel\\cache"; }

void* createPreviewChild(void* parentHwnd) {
    HWND parent = static_cast<HWND>(parentHwnd);
    if (!IsWindow(parent)) return nullptr;
    RECT rc;
    GetClientRect(parent, &rc);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;  // CS_OWNDC: required for a GL pixel format
    wc.lpfnWndProc = previewProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ImageTunnelPreview";
    RegisterClassW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0,
                           rc.right - rc.left, rc.bottom - rc.top, parent, nullptr, wc.hInstance, nullptr);
}

bool windowAlive(void* hwnd) { return hwnd && IsWindow(static_cast<HWND>(hwnd)); }

bool acquireSingleInstance() {
    // Intentionally leaked: the mutex lives until the process exits.
    CreateMutexW(nullptr, TRUE, L"Local\\ImageTunnelScreensaver");
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

void showError(const std::string& title, const std::string& message) {
    std::fprintf(stderr, "%s: %s\n", title.c_str(), message.c_str());
    auto widen = [](const std::string& s) {
        std::wstring w(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), static_cast<int>(w.size()));
        return w;
    };
    MessageBoxW(nullptr, widen(message).c_str(), widen(title).c_str(), MB_OK | MB_ICONERROR);
}

#else
namespace {
std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}
std::string xdgDir(const char* xdgVar, const char* homeSub) {
    const std::string xdg = envOr(xdgVar, "");
    if (!xdg.empty()) return xdg + "/imagetunnel";
    return envOr("HOME", "/tmp") + "/" + homeSub + "/imagetunnel";
}
}  // namespace

std::string configDir() { return xdgDir("XDG_CONFIG_HOME", ".config"); }
std::string cacheDir() { return xdgDir("XDG_CACHE_HOME", ".cache"); }
void* createPreviewChild(void*) { return nullptr; }
bool windowAlive(void*) { return false; }
bool acquireSingleInstance() { return true; }
void showError(const std::string& title, const std::string& message) {
    std::fprintf(stderr, "%s: %s\n", title.c_str(), message.c_str());
}
#endif

}  // namespace it::platform
