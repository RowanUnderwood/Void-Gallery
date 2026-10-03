#pragma once

#include <SDL.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "assets/AsyncLoader.h"
#include "assets/TextureStreamer.h"
#include "config/Config.h"
#include "modes/Mode.h"
#include "modes/MovingLights.h"
#include "render/Gl.h"
#include "render/Renderer.h"
#include "ui/Ui.h"
#include "util/FramePacer.h"
#include "util/FrameStats.h"
#include "util/GpuTimer.h"
#include "util/Thermal.h"

namespace it {

class MazeMode;

// Windows screensaver protocol: /s run, /c configure, /p <hwnd> preview. None = normal program.
enum class SaverMode { None, Run, Config, Preview };

struct AppOptions {
    std::string source;          // nginx base URL or local web-root directory (overrides the saved one)
    std::string configSpec;      // default: tunnel_config_potato.json (Pi) / tunnel_config.json (desktop)
    std::string cacheDir;        // default: platform::cacheDir()
    std::string configDir;       // default: platform::configDir()
    bool windowed = false;
    int width = 1920, height = 1080, refresh = 60;  // Pi display mode
    int rotation = -1;  // --rotate: monitor rotation (deg CW); overrides config when >= 0
    int frameCap = -1;  // --frame-cap: 30 | 60 | 120 overrides config; 0 = vsync off / unlocked
    int decodeThreads = 0;  // 0 = auto
    std::string benchMode;
    float benchSeconds = 120.0f;
    std::string benchCsv;
    SaverMode saver = SaverMode::None;
    void* previewParent = nullptr;  // HWND for /p
};

// Exit codes understood by deploy/imagetunnel.service.
constexpr int kExitRestart = 0;    // Esc / crash: systemd restarts the app
constexpr int kExitStop = 10;      // Ctrl+Q: RestartPreventExitStatus=10
constexpr int kExitBenchFail = 2;

class App {
public:
    int run(const AppOptions& opts);

private:
    bool initWindow();
    void createBlankWindows();
    void shutdown();

    void loadConfig(bool useOverride);
    std::string resolveSource(const json* local) const;
    void applySource();
    bool fetchJson(const std::string& spec, json& out);
    void saveConfig();
    void reloadFromServer();
    void loadSequence();
    void switchMode(const std::string& name);
    void rebuildMode();
    void configureStreamer();
    void applySwapInterval();
    void applyActions(const UiActions& a);
    void handleEvents();
    void handleDrop();
    void toggleFullscreen();
    void frame();
    void renderScene(bool stereo);

    float effectiveRenderScale() const;
    bool updateRandomMode(float dt, bool loading);
    int effectiveTextureEdge(int preferred) const;

    AppOptions opts_;
    std::string overridePath_;
    std::string sourceSpec_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    std::vector<SDL_Window*> blankWindows_;  // screensaver: black windows on the other monitors
    void* previewChild_ = nullptr;
    int screenW_ = 0, screenH_ = 0;    // scan-out (window) size
    int logicalW_ = 1, logicalH_ = 1;  // virtual screen after monitor rotation
    float refreshHz_ = 60.0f;
    bool pace30_ = false;              // Pi: 30 fps via paced swaps (KMSDRM ignores interval 2)
    FramePacer pacer_;                 // desktop: 30 / 60 / 120 / unlocked

    Config cfg_;
    std::shared_ptr<ImageSource> source_;
    std::unordered_map<std::string, std::string> manifest_;
    std::vector<std::string> customFiles_;   // drag & dropped images (desktop)
    std::vector<std::string> dropPending_;

    std::unique_ptr<AsyncLoader> loader_;
    std::unique_ptr<TextureStreamer> streamer_;
    std::unique_ptr<Renderer> gfx_;
    std::unique_ptr<gl::SceneTarget> target_;
    Camera cam_;
    MovingLights lights_;
    std::unique_ptr<ModeContext> ctx_;
    std::map<std::string, std::unique_ptr<Mode>> modes_;
    Mode* mode_ = nullptr;
    MazeMode* maze_ = nullptr;

    Ui ui_;
    FrameStats stats_;
    BenchRecorder bench_;
    AutoQuality autoQ_;
    std::unique_ptr<ThermalMonitor> thermal_;
    std::unique_ptr<GpuTimer> gpuTimer_;

    bool running_ = true;
    int exitCode_ = kExitRestart;
    bool benchStarted_ = false;
    bool benchPassed_ = true;
    float time_ = 0.0f;
    float readySince_ = -1.0f;
    float mouseTravel_ = 0.0f;         // screensaver: exit once the mouse really moves
    uint64_t lastFrame_ = 0;
    uint64_t lastSwap_ = 0;
    uint64_t frameIndex_ = 0;
    bool cursorShown_ = true;
    bool relativeMouse_ = false;       // rotated monitor: own cursor from relative motion
    glm::vec2 cursor_{0.0f};           // mouse position in logical (rotated) pixels
    enum class RandomPhase { None, Out, In };
    RandomPhase randomPhase_ = RandomPhase::None;
    float randomFade_ = 0.0f;
    bool glDebug_ = false;  // IT_GL_DEBUG=1: report glGetError() every frame
};

}  // namespace it
