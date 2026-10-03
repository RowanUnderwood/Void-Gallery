// Image Tunnel - native Raspberry Pi 5 port of index.html. See README.md / PI_NATIVE_PLAN.md.

#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "App.h"

namespace {

void usage() {
    std::printf(
        "Usage: imagetunnel [options]\n"
        "  --source URL|DIR     nginx base URL (http://host/) or local web-root directory\n"
        "                       (default http://localhost/)\n"
        "  --config SPEC        base config: path relative to source, absolute file or URL\n"
        "                       (default tunnel_config_potato.json)\n"
        "  --cache DIR          download cache (default ~/.cache/imagetunnel)\n"
        "  --config-dir DIR     local settings override dir (default ~/.config/imagetunnel)\n"
        "  --size WxH           display mode (default 1920x1080)\n"
        "  --refresh HZ         display refresh (default 60)\n"
        "  --rotate DEG         monitor rotation clockwise: 0|90|180|270 (90 = portrait)\n"
        "  --frame-cap FPS      30 or 60 (vsync-locked)\n"
        "  --windowed           1280x720 window (desktop development)\n"
        "  --threads N          decode worker threads (default 3)\n"
        "  --bench MODE         run MODE (floating|tunnel|grid|maze) and report frame times\n"
        "  --seconds N          benchmark duration (default 120)\n"
        "  --csv PATH           write per-frame benchmark CSV\n"
        "Keys: H settings, P performance overlay, R reload server config, Esc quit (restart),\n"
        "      Ctrl+Q quit (stay stopped)\n");
}

}  // namespace

int main(int argc, char** argv) {
    it::AppOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(64);
            }
            return argv[++i];
        };
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
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage();
            return 64;
        }
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // line-buffered so journald shows output immediately
    curl_global_init(CURL_GLOBAL_DEFAULT);
    it::App app;
    const int rc = app.run(o);
    curl_global_cleanup();
    return rc;
}
