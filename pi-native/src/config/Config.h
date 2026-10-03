#pragma once
// Settings model mirroring the web viewer's v2.2 JSON (tunnel_config.json / tunnel_config_potato.json).
// Unknown keys (e.g. the stereo settings the Pi build drops) are preserved in `raw` so a saved file
// stays usable by index.html and the Gradio settings tab.

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace it {

using json = nlohmann::json;

constexpr double kConfigVersion = 2.2;

// X(type, name) list of the per-mode "stateKeys" from index.html, minus the stereo keys.
#define IT_MODE_FIELDS(X)                                                                         \
    X(double, cameraSpeed) X(double, fogDensity) X(double, tunnelRadius) X(double, imageSize)     \
    X(int, tunnelRows) X(int, totalImages) X(double, rotationSpeed) X(bool, noRotation)           \
    X(int, tunnelRotation) X(int, lightCount) X(double, ambientIntensity)                         \
    X(int, gridCols) X(int, gridRows) X(double, gridSpacing) X(double, pathRandomness)            \
    X(int, gridEdgeBuffer) X(double, lightSpeed) X(double, lightIntensity)                        \
    X(std::string, lightColorMode) X(int, maxUploadsPerFrame) X(int, anisotropyLevel)             \
    X(bool, showMazeMap) X(int, mazeComplexity) X(double, mazeWalkingSpeed)                       \
    X(double, mazeTextureTiling) X(int, mazeImageCount) X(double, mazeSpotlightAngle)             \
    X(double, mazeSpotlightHeight) X(int, mazeShadowRes) X(std::string, navStyle)                 \
    X(std::string, mazeWallTexture) X(std::string, mazeFloorTexture) X(std::string, mazeCeilingTexture)     X(double, stereoEyeSep) X(double, stereoFOVBoost)

struct ModeSettings {
    // Defaults match the `config` object in index.html.
    double cameraSpeed = 1.0;
    double fogDensity = 0.0035;
    double tunnelRadius = 85.0;
    double imageSize = 60.0;
    int tunnelRows = 5;
    int totalImages = 20;
    double rotationSpeed = 0.1;
    bool noRotation = true;
    int tunnelRotation = 0;
    int lightCount = 15;
    double ambientIntensity = 0.5;
    int gridCols = 6;
    int gridRows = 4;
    double gridSpacing = 400.0;
    double pathRandomness = 0.3;
    int gridEdgeBuffer = 2;
    double lightSpeed = 1.0;
    double lightIntensity = 1500.0;
    std::string lightColorMode = "white";
    int maxUploadsPerFrame = 2;
    int anisotropyLevel = 1;
    bool showMazeMap = true;
    int mazeComplexity = 21;
    double mazeWalkingSpeed = 1.0;
    double mazeTextureTiling = 2.0;
    int mazeImageCount = 55;
    double mazeSpotlightAngle = 3.14159265358979 / 5.0;
    double mazeSpotlightHeight = 6.0;
    int mazeShadowRes = 1024;
    std::string navStyle = "win95";
    std::string mazeWallTexture = "Bricks003_1K-JPG";
    std::string mazeFloorTexture = "WoodFloor071_1K-PNG";
    std::string mazeCeilingTexture = "OfficeCeiling001_1K-PNG";
    double stereoEyeSep = 0.064;       // 3D SBS (desktop builds only)
    double stereoFOVBoost = 30.0;
};

// Native-app settings (Pi and Windows), stored under globals.pi (ignored by the web viewer).
struct PiSettings {
    int frameCap = 60;                 // Pi: 30 | 60. Desktop: 30 | 60 | 120 | 0 (= unlocked, for VRR)
    int rotation = 0;                  // monitor rotation, degrees clockwise: 0 | 90 | 180 | 270
    bool randomMode = false;           // "random" display mode: cycle through the four modes
    double randomInterval = 60.0;      // seconds per mode in random mode (counted once it has loaded)
    double renderScale = 1.0;          // 3D scene resolution scale (UI stays native)
    int msaa = 0;                      // Pi: 0 | 4. Desktop: 0 | 2 | 4 | 8
    int maxTextureEdge = 1024;         // decode-time downscale limit
    int uploadBytesPerFrame = 6 * 1024 * 1024;
    std::string mazeShadows = "off";   // "off" | "nearest" | "all" (all = desktop only)
    int maxLights = 4;                 // point/spot lights evaluated per draw (caps::kMaxLightsPerDraw)
    bool normalMaps = true;
    bool roughnessMaps = false;        // maze: per-texel specular from the _Roughness map
    std::string qualityPreset = "custom";  // last preset applied from the settings panel
    std::string source;                // image source (URL or folder); the CLI --source overrides it
    std::string regenTransition = "fade";
    bool autoQuality = false;
    int textureMemoryMB = 600;         // budget used to pick per-mode texture size
    std::string localFolder;           // replaces browser upload: images loaded from this directory
    bool useLocalFolder = false;
};

struct Globals {
    std::string serverPath = "images";
    std::string quality = "quarter";   // full | half | quarter
    int serverStart = 1;
    int serverEnd = 1075;
    bool showStats = false;
    bool useStereo = false;            // 3D SBS (desktop builds only; the Pi build ignores it)
    std::vector<std::string> availableTextures = {"Bricks003_1K-JPG", "OfficeCeiling001_1K-PNG",
                                                  "Tiles084_1K-PNG", "WoodFloor071_1K-PNG"};
    std::vector<std::string> imageFolders = {"transparentimages", "movieposters", "images", "AIimages"};
    PiSettings pi;
};

// Hardware limits per platform. The Pi values are the cuts from PI_NATIVE_PLAN.md section 3; the
// desktop values restore index.html's ranges (and a bit more where a desktop GPU allows it).
namespace caps {
#if defined(IT_DESKTOP_GL)
constexpr bool kDesktop = true;
constexpr int kMaxTunnelRows = 50;
constexpr int kMaxGridDim = 20;
constexpr int kMaxFloating = 500;
constexpr int kMaxMazeComplexity = 41;
constexpr int kMaxMazeImages = 200;
constexpr int kMaxLightCount = 20;
constexpr int kMaxLightsPerDraw = 8;
constexpr int kMaxAnisotropy = 16;
constexpr int kMaxShadowRes = 4096;
constexpr int kMaxMsaa = 8;
constexpr double kMaxRenderScale = 2.0;
constexpr int kMaxSpots = 6;
#else
constexpr bool kDesktop = false;
constexpr int kMaxTunnelRows = 20;
constexpr int kMaxGridDim = 12;
constexpr int kMaxFloating = 150;
constexpr int kMaxMazeComplexity = 31;
constexpr int kMaxMazeImages = 120;
constexpr int kMaxLightCount = 8;
constexpr int kMaxLightsPerDraw = 4;
constexpr int kMaxAnisotropy = 4;
constexpr int kMaxShadowRes = 512;
constexpr int kMaxMsaa = 4;
constexpr double kMaxRenderScale = 1.0;
constexpr int kMaxSpots = 4;
#endif
// Shader array sizes (must be >= the largest platform value above).
constexpr int kShaderLights = 8;
constexpr int kShaderSpots = 6;
}  // namespace caps

struct Config {
    std::string activeMode = "tunnel";
    Globals globals;
    std::map<std::string, ModeSettings> modes;

    // Runtime-only values (index.html does not persist these either).
    double opacity = 1.0;
    double wobbleStrength = 0.5;
    double wobbleSpeed = 0.5;

    // Merged source documents; written back on save so unknown keys survive.
    json raw = json::object();

    Config();

    static const std::vector<std::string>& modeNames();
    static ModeSettings defaultsFor(const std::string& mode);

    ModeSettings& cur() { return modes.at(activeMode); }
    const ModeSettings& cur() const { return modes.at(activeMode); }

    // Applies a v2.2 document on top of the current values (same semantics as applyConfiguration()).
    void apply(const json& doc);
    // Builds a v2.2 document from current values on top of `raw`.
    json toJson() const;
    // Clamps all values to this platform's limits (caps::).
    void clampToPlatform();
};

std::string qualityFolder(const std::string& quality);  // "" | "halfres/" | "quarterres/"

// Desktop quality presets: "low" | "medium" | "high" | "ultra". Sets render quality only (scale,
// MSAA, shadows, lights per draw, material maps, texture size, anisotropy, image resolution),
// never scene content (counts, sizes, speeds).
// Returns false for unknown names (e.g. "custom", which leaves everything as is).
bool applyQualityPreset(Config& c, const std::string& name);

}  // namespace it
