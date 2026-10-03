#include "config/Config.h"

#include <algorithm>
#include <chrono>
#include <type_traits>

namespace it {

namespace {

template <class T>
void readField(const json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return;
    try {
        if constexpr (std::is_same_v<T, bool>) {
            if (it->is_boolean()) out = it->template get<bool>();
            else if (it->is_number()) out = it->template get<double>() != 0.0;
        } else if constexpr (std::is_arithmetic_v<T>) {
            if (it->is_number()) out = static_cast<T>(it->template get<double>());
            else if (it->is_boolean()) out = static_cast<T>(it->template get<bool>());
        } else {
            out = it->template get<T>();
        }
    } catch (const std::exception&) {
        // Wrong type in the file: keep the current value.
    }
}

void readMode(const json& j, ModeSettings& m) {
#define IT_READ(type, name) readField(j, #name, m.name);
    IT_MODE_FIELDS(IT_READ)
#undef IT_READ
}

void writeMode(json& j, const ModeSettings& m) {
#define IT_WRITE(type, name) j[#name] = m.name;
    IT_MODE_FIELDS(IT_WRITE)
#undef IT_WRITE
}

void readPi(const json& j, PiSettings& p) {
    readField(j, "frameCap", p.frameCap);
    readField(j, "rotation", p.rotation);
    readField(j, "randomMode", p.randomMode);
    readField(j, "randomInterval", p.randomInterval);
    readField(j, "renderScale", p.renderScale);
    readField(j, "msaa", p.msaa);
    readField(j, "maxTextureEdge", p.maxTextureEdge);
    readField(j, "uploadBytesPerFrame", p.uploadBytesPerFrame);
    readField(j, "mazeShadows", p.mazeShadows);
    readField(j, "maxLights", p.maxLights);
    readField(j, "normalMaps", p.normalMaps);
    readField(j, "regenTransition", p.regenTransition);
    readField(j, "autoQuality", p.autoQuality);
    readField(j, "textureMemoryMB", p.textureMemoryMB);
    readField(j, "localFolder", p.localFolder);
    readField(j, "roughnessMaps", p.roughnessMaps);
    readField(j, "qualityPreset", p.qualityPreset);
    readField(j, "source", p.source);
    readField(j, "useLocalFolder", p.useLocalFolder);
}

void writePi(json& j, const PiSettings& p) {
    j["frameCap"] = p.frameCap;
    j["rotation"] = p.rotation;
    j["randomMode"] = p.randomMode;
    j["randomInterval"] = p.randomInterval;
    j["renderScale"] = p.renderScale;
    j["msaa"] = p.msaa;
    j["maxTextureEdge"] = p.maxTextureEdge;
    j["uploadBytesPerFrame"] = p.uploadBytesPerFrame;
    j["mazeShadows"] = p.mazeShadows;
    j["maxLights"] = p.maxLights;
    j["normalMaps"] = p.normalMaps;
    j["regenTransition"] = p.regenTransition;
    j["autoQuality"] = p.autoQuality;
    j["textureMemoryMB"] = p.textureMemoryMB;
    j["localFolder"] = p.localFolder;
    j["roughnessMaps"] = p.roughnessMaps;
    j["qualityPreset"] = p.qualityPreset;
    j["source"] = p.source;
    j["useLocalFolder"] = p.useLocalFolder;
}

int clampOdd(int v, int lo, int hi) {
    v = std::clamp(v, lo, hi);
    if (v % 2 == 0) v = (v + 1 <= hi) ? v + 1 : v - 1;
    return v;
}

}  // namespace

const std::vector<std::string>& Config::modeNames() {
    static const std::vector<std::string> names = {"floating", "tunnel", "grid", "maze"};
    return names;
}

// Per-mode defaults copied from `modeStore` in index.html.
ModeSettings Config::defaultsFor(const std::string& mode) {
    ModeSettings m;
    if (mode == "floating") {
        m.stereoEyeSep = 0.5;
        m.noRotation = false;
        m.lightCount = 15;
        m.anisotropyLevel = 1;
    } else if (mode == "tunnel") {
        m.stereoEyeSep = 0.1;
        m.stereoFOVBoost = 50;
        m.fogDensity = 0.0025;
        m.tunnelRows = 10;
        m.totalImages = 10;
        m.tunnelRotation = 270;
        m.lightCount = 2;
        m.ambientIntensity = 0.2;
        m.lightSpeed = 5.0;
        m.lightIntensity = 800;
        m.lightColorMode = "random";
        m.maxUploadsPerFrame = 1;
        m.anisotropyLevel = 4;
    } else if (mode == "grid") {
        m.stereoEyeSep = 0.8;
        m.cameraSpeed = 1.5;
        m.fogDensity = 0.0015;
        m.imageSize = 50;
        m.rotationSpeed = 0.0;
        m.lightCount = 5;
        m.gridCols = 8;
        m.gridRows = 6;
        m.gridSpacing = 300;
        m.pathRandomness = 0.5;
        m.lightSpeed = 2.0;
        m.maxUploadsPerFrame = 4;
        m.anisotropyLevel = 4;
    } else if (mode == "maze") {
        m.stereoEyeSep = 0.5;
        m.fogDensity = 0.012;
        m.imageSize = 40;
        m.rotationSpeed = 0.0;
        m.lightCount = 0;
        m.ambientIntensity = 0.08;
        m.lightIntensity = 800;
        m.maxUploadsPerFrame = 1;
        m.anisotropyLevel = 4;
        m.mazeShadowRes = 512;
    }
    return m;
}

Config::Config() {
    for (const auto& name : modeNames()) modes[name] = defaultsFor(name);
    if (caps::kDesktop) {
        auto& pi = globals.pi;
        pi.frameCap = 60;
        pi.maxLights = caps::kMaxLightsPerDraw;
        pi.uploadBytesPerFrame = 32 * 1024 * 1024;
        pi.textureMemoryMB = 4096;
        pi.qualityPreset = "ultra";  // tuned on an RTX 5090 @ 3840x2160; pick a lower preset for weaker GPUs
        applyQualityPreset(*this, pi.qualityPreset);
    }
}

bool applyQualityPreset(Config& c, const std::string& name) {
    struct Preset {
        const char* name;
        double renderScale;
        int msaa;
        const char* shadows;
        int shadowRes;
        int lightsPerDraw;
        bool roughness;
        int textureEdge;
        int anisotropy;
        const char* images;  // server folder resolution: quarter | half | full
    };
    // ultra = every quality setting at its maximum. Measured on the dev machine (RTX 5090, 3840x2160,
    // vsync off): worst GPU p99 1.4 ms per frame, and 4 ms with every content limit maxed + 3D SBS,
    // i.e. far above 60 fps. Lower presets are for weaker GPUs / integrated graphics.
    static const Preset presets[] = {
        {"low", 0.75, 0, "off", 1024, 4, false, 1024, 4, "quarter"},
        {"medium", 1.0, 2, "nearest", 1024, 6, false, 1024, 8, "half"},
        {"high", 1.0, 4, "all", 2048, 8, true, 2048, 16, "half"},
        {"ultra", 2.0, 8, "all", 4096, 8, true, 4096, 16, "full"},
    };
    for (const auto& p : presets) {
        if (name != p.name) continue;
        auto& pi = c.globals.pi;
        pi.renderScale = p.renderScale;
        pi.msaa = p.msaa;
        pi.mazeShadows = p.shadows;
        pi.maxLights = p.lightsPerDraw;
        pi.normalMaps = true;
        pi.roughnessMaps = p.roughness;
        pi.maxTextureEdge = p.textureEdge;
        c.globals.quality = p.images;
        for (auto& [mode, m] : c.modes) {
            m.anisotropyLevel = p.anisotropy;
            m.mazeShadowRes = p.shadowRes;
        }
        pi.qualityPreset = name;
        return true;
    }
    return false;
}

void Config::apply(const json& doc) {
    if (!doc.is_object()) return;
    if (auto g = doc.find("globals"); g != doc.end() && g->is_object()) {
        readField(*g, "serverPath", globals.serverPath);
        readField(*g, "quality", globals.quality);
        readField(*g, "serverStart", globals.serverStart);
        readField(*g, "serverEnd", globals.serverEnd);
        readField(*g, "showStats", globals.showStats);
        readField(*g, "useStereo", globals.useStereo);
        readField(*g, "availableTextures", globals.availableTextures);
        readField(*g, "imageFolders", globals.imageFolders);
        if (auto p = g->find("pi"); p != g->end() && p->is_object()) readPi(*p, globals.pi);
    }
    if (auto ms = doc.find("modes"); ms != doc.end() && ms->is_object()) {
        for (auto it = ms->begin(); it != ms->end(); ++it) {
            auto mode = modes.find(it.key());
            if (mode != modes.end() && it->is_object()) readMode(*it, mode->second);
        }
    }
    if (auto am = doc.find("activeMode"); am != doc.end() && am->is_string()) {
        const std::string m = am->get<std::string>();
        if (modes.count(m)) activeMode = m;
    }
    raw.merge_patch(doc);
}

json Config::toJson() const {
    json j = raw.is_object() ? raw : json::object();
    j["version"] = kConfigVersion;
    j["savedAt"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    j["activeMode"] = activeMode;

    json& g = j["globals"];
    if (!g.is_object()) g = json::object();
    g["serverPath"] = globals.serverPath;
    g["quality"] = globals.quality;
    g["serverStart"] = globals.serverStart;
    g["serverEnd"] = globals.serverEnd;
    g["showStats"] = globals.showStats;
    g["useStereo"] = globals.useStereo;
    g["availableTextures"] = globals.availableTextures;
    g["imageFolders"] = globals.imageFolders;
    json& p = g["pi"];
    if (!p.is_object()) p = json::object();
    writePi(p, globals.pi);

    json& ms = j["modes"];
    if (!ms.is_object()) ms = json::object();
    for (const auto& [name, m] : modes) {
        json& mj = ms[name];
        if (!mj.is_object()) mj = json::object();
        writeMode(mj, m);
    }
    return j;
}

void Config::clampToPlatform() {
    for (auto& [name, m] : modes) {
        m.tunnelRows = std::clamp(m.tunnelRows, 1, caps::kMaxTunnelRows);
        m.gridCols = std::clamp(m.gridCols, 2, caps::kMaxGridDim);
        m.gridRows = std::clamp(m.gridRows, 2, caps::kMaxGridDim);
        m.totalImages = std::clamp(m.totalImages, 1, caps::kMaxFloating);
        m.mazeComplexity = clampOdd(m.mazeComplexity, 11, caps::kMaxMazeComplexity);
        m.mazeImageCount = std::clamp(m.mazeImageCount, 0, caps::kMaxMazeImages);
        m.lightCount = std::clamp(m.lightCount, 0, caps::kMaxLightCount);
        m.anisotropyLevel = std::clamp(m.anisotropyLevel, 1, caps::kMaxAnisotropy);
        int res = 256;  // power of two in [256, kMaxShadowRes]
        while (res < m.mazeShadowRes && res < caps::kMaxShadowRes) res *= 2;
        m.mazeShadowRes = res;
        m.stereoEyeSep = std::clamp(m.stereoEyeSep, 0.0, 2.0);
        m.stereoFOVBoost = std::clamp(m.stereoFOVBoost, 0.0, 80.0);
        m.maxUploadsPerFrame = std::clamp(m.maxUploadsPerFrame, 1, 20);
        m.tunnelRotation = ((m.tunnelRotation % 360) + 360) % 360 / 90 * 90;
        m.mazeWalkingSpeed = std::clamp(m.mazeWalkingSpeed, 0.1, 5.0);
        if (m.navStyle != "win95" && m.navStyle != "modern") m.navStyle = "win95";
        if (m.lightColorMode != "white" && m.lightColorMode != "random") m.lightColorMode = "white";
    }
    auto& pi = globals.pi;
    if (caps::kDesktop) {
        pi.frameCap = pi.frameCap <= 0 ? 0 : pi.frameCap <= 30 ? 30 : pi.frameCap <= 60 ? 60 : 120;
    } else {
        pi.frameCap = pi.frameCap <= 30 ? 30 : 60;
    }
    pi.rotation = ((pi.rotation % 360) + 360) % 360 / 90 * 90;
    pi.randomInterval = std::clamp(pi.randomInterval, 10.0, 3600.0);
    pi.renderScale = std::clamp(pi.renderScale, 0.5, caps::kMaxRenderScale);
    pi.msaa = pi.msaa >= 8 ? 8 : pi.msaa >= 4 ? 4 : pi.msaa >= 2 ? 2 : 0;
    pi.msaa = std::min(pi.msaa, caps::kMaxMsaa);
    pi.maxTextureEdge = std::clamp(pi.maxTextureEdge, 128, 2048);
    pi.uploadBytesPerFrame = std::clamp(pi.uploadBytesPerFrame, 256 * 1024, 64 * 1024 * 1024);
    pi.maxLights = std::clamp(pi.maxLights, 0, caps::kMaxLightsPerDraw);
    pi.textureMemoryMB = std::clamp(pi.textureMemoryMB, 128, 4096);
    if (pi.mazeShadows != "off" && pi.mazeShadows != "nearest" && pi.mazeShadows != "all") pi.mazeShadows = "off";
    if (!caps::kDesktop && pi.mazeShadows == "all") pi.mazeShadows = "nearest";
    if (pi.regenTransition != "fade" && pi.regenTransition != "cut" && pi.regenTransition != "flip")
        pi.regenTransition = "fade";
    if (!caps::kDesktop) {
        if (pi.regenTransition == "flip") pi.regenTransition = "fade";
        pi.roughnessMaps = false;
        // useStereo is left as-is (the web viewer's setting); the Pi renderer simply ignores it.
    }
    if (globals.quality != "full" && globals.quality != "half" && globals.quality != "quarter")
        globals.quality = "quarter";
    globals.serverStart = std::max(1, globals.serverStart);
    globals.serverEnd = std::max(globals.serverStart, globals.serverEnd);
}

std::string qualityFolder(const std::string& quality) {
    if (quality == "half") return "halfres/";
    if (quality == "quarter") return "quarterres/";
    return "";
}

}  // namespace it
