#pragma once
// Dear ImGui overlays: settings panel (replaces lil-gui), stats overlay (replaces stats.js),
// loading overlay and toasts.

#include <SDL.h>

#include <string>

#include "config/Config.h"

namespace it {

class FrameStats;

struct UiActions {
    bool modeChanged = false;
    std::string newMode;
    bool rebuild = false;          // rebuild current mode scene
    bool reloadSequence = false;   // image list changed (folder / quality / range / local folder)
    bool resetReload = false;      // drop buffered textures and rebuild
    bool lightsChanged = false;    // moving light count / colour mode
    bool materialsChanged = false; // maze textures
    bool anisoChanged = false;
    bool frameCapChanged = false;
    bool save = false;
    bool reloadServer = false;
    bool quit = false;
    // desktop / screensaver
    bool sourceChanged = false;    // image source field applied
    bool clearCustom = false;      // drop the drag & dropped files, back to server/folder images
    bool toggleFullscreen = false;
    bool saveAndClose = false;     // screensaver settings window: Save & Close
    bool closeNoSave = false;
    bool presetChanged = false;    // quality preset picked
};

// What the settings panel needs to know about the running program.
struct PanelContext {
    bool configMode = false;       // Windows screensaver settings window (/c)
    bool windowed = false;
    bool hasCustomFiles = false;
    std::string pacing;            // frame pacing description
};

struct StatsInfo {
    const FrameStats* frames = nullptr;
    float gpuMs = 0.0f;  // desktop GL timer queries; 0 = unavailable
    int drawCalls = 0;
    size_t texturesResident = 0;
    size_t textureMB = 0;
    size_t texturesReady = 0;
    size_t texturesInflight = 0;
    size_t sequenceSize = 0;
    float temperatureC = -1;
    std::string throttled;
    int autoQualityLevel = 0;
    std::string source;
    bool offline = false;
    std::string modeStatus;
    std::string lastImage;
    std::string lastError;
    int renderW = 0, renderH = 0;
};

class Ui {
public:
    bool init(SDL_Window* window, SDL_GLContext ctx, int minScreenDim, const char* glslVersion);
    void shutdown();
    bool processEvent(const SDL_Event& e);  // true if ImGui wants the event
    bool wantsKeyboard() const;

    void beginFrame(int logicalW, int logicalH);
    void endFrame();  // renders draw data into the bound framebuffer

    UiActions settingsPanel(Config& cfg, float maxAniso, const PanelContext& pc);
    void statsOverlay(const StatsInfo& info);
    void loadingOverlay(const std::string& title, const std::string& sub);
    void toast(const std::string& msg, float seconds = 2.5f);
    bool toastActive() const;
    void setSoftwareCursor(bool on);  // ImGui draws the pointer (rotated displays)
    void drawToast();

    bool settingsVisible = false;

private:
    bool initialized_ = false;
    std::string toast_;
    double toastUntil_ = 0;
};

}  // namespace it
