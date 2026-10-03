// Image Tunnel - native port of index.html for the Raspberry Pi 5 (kiosk) and Windows (.scr
// screensaver). See README.md / ../PI_NATIVE_PLAN.md.

#define SDL_MAIN_HANDLED  // we provide main / WinMain ourselves

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "App.h"
#include "assets/HttpClient.h"

#if defined(IT_WINDOWS)
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

void usage() {
    std::printf(
        "Usage: imagetunnel [options]\n"
        "  --source URL|DIR     nginx base URL (http://host:port/) or local web-root directory\n"
        "                       (Pi default http://localhost/; Windows: the one saved in the settings)\n"
        "  --config SPEC        base config: path relative to source, absolute file or URL\n"
        "                       (default tunnel_config_potato.json on the Pi, tunnel_config.json on Windows)\n"
        "  --cache DIR          download cache\n"
        "  --config-dir DIR     local settings directory\n"
        "  --size WxH           Pi display mode (default 1920x1080)\n"
        "  --refresh HZ         Pi display refresh (default 60)\n"
        "  --rotate DEG         monitor rotation clockwise: 0|90|180|270 (90 = portrait)\n"
        "  --frame-cap FPS      30 | 60 | 120 (vsync-locked; 120 desktop only), 0 = unlocked / vsync off\n"
        "  --windowed           window instead of fullscreen\n"
        "  --threads N          decode worker threads (default: auto)\n"
        "  --bench MODE         run MODE (floating|tunnel|grid|maze) and report frame times\n"
        "  --seconds N          benchmark duration (default 120)\n"
        "  --csv PATH           write per-frame benchmark CSV\n"
        "Windows screensaver arguments: /s (run), /c (settings), /p <hwnd> (preview); none = settings.\n"
        "Keys: H settings, P performance overlay, R reload server config, F11 fullscreen, Esc quit,\n"
        "      Ctrl+Q quit (Pi: stay stopped)\n");
}

// "/s", "-S", "/c:1234", "/p 1234" -> mode letter and optional handle.
bool parseSaverArg(const std::string& a, char& letter, std::string& rest) {
    if (a.size() < 2 || (a[0] != '/' && a[0] != '-')) return false;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(a[1])));
    if (c != 's' && c != 'c' && c != 'p' && c != 'a') return false;
    if (a.size() > 2 && a[2] != ':') return false;
    letter = c;
    rest = a.size() > 3 ? a.substr(3) : std::string();
    return true;
}

int realMain(int argc, char** argv) {
    it::AppOptions o;
    bool sawArgs = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        sawArgs = true;
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(64);
            }
            return argv[++i];
        };
        char letter = 0;
        std::string rest;
        if (a == "--source") o.source = next("--source");
        else if (a == "--config") o.configSpec = next("--config");
        else if (a == "--cache") o.cacheDir = next("--cache");
        else if (a == "--config-dir") o.configDir = next("--config-dir");
        else if (a == "--size") {
            const std::string v = next("--size");
            if (std::sscanf(v.c_str(), "%dx%d", &o.width, &o.height) != 2) {
                std::fprintf(stderr, "bad --size '%s'\n", v.c_str());
                return 64;
            }
        } else if (a == "--refresh") o.refresh = std::atoi(next("--refresh").c_str());
        else if (a == "--windowed") o.windowed = true;
        else if (a == "--rotate") o.rotation = std::atoi(next("--rotate").c_str());
        else if (a == "--frame-cap") o.frameCap = std::atoi(next("--frame-cap").c_str());
        else if (a == "--threads") o.decodeThreads = std::atoi(next("--threads").c_str());
        else if (a == "--bench") o.benchMode = next("--bench");
        else if (a == "--seconds") o.benchSeconds = static_cast<float>(std::atof(next("--seconds").c_str()));
        else if (a == "--csv") o.benchCsv = next("--csv");
        else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else if (parseSaverArg(a, letter, rest)) {
            if (letter == 's') o.saver = it::SaverMode::Run;
            else if (letter == 'c') o.saver = it::SaverMode::Config;
            else if (letter == 'a') return 0;  // password change (Win9x only)
            else {
                if (rest.empty() && i + 1 < argc) rest = argv[++i];
                o.saver = it::SaverMode::Preview;
                o.previewParent = reinterpret_cast<void*>(static_cast<uintptr_t>(std::strtoull(rest.c_str(), nullptr, 10)));
                if (!o.previewParent) return 0;
            }
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage();
            return 64;
        }
    }
#if defined(IT_WINDOWS) && !defined(IT_CONSOLE)
    // Double-clicking a .scr or "Configure" without a handle: show the settings window.
    if (!sawArgs) o.saver = it::SaverMode::Config;
#else
    (void)sawArgs;
#endif
#if defined(_WIN32)
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // the UCRT rejects _IOLBF with size 0; unbuffered = log is always current
#else
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffered so journald shows output immediately
#endif
    SDL_SetMainReady();
    it::httpGlobalInit();
    it::App app;
    const int rc = app.run(o);
    it::httpGlobalCleanup();
    return rc;
}

}  // namespace

#if defined(IT_WINDOWS) && !defined(IT_CONSOLE)
// GUI subsystem entry point: rebuild a UTF-8 argv from the wide command line. A screensaver has no
// console, so stdout/stderr go to %LOCALAPPDATA%\ImageTunnel\imagetunnel.log (overwritten per run).
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (GetStdHandle(STD_OUTPUT_HANDLE) == nullptr || GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN) {
        wchar_t local[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) {
            std::wstring dir = std::wstring(local) + L"\\ImageTunnel";
            CreateDirectoryW(dir.c_str(), nullptr);
            const std::wstring log = dir + L"\\imagetunnel.log";
            if (_wfreopen(log.c_str(), L"w", stdout)) _wfreopen(log.c_str(), L"a", stderr);
        }
    }
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), n, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    LocalFree(wargv);
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);
    return realMain(static_cast<int>(args.size()), argv.data());
}
#else
int main(int argc, char** argv) { return realMain(argc, argv); }
#endif
