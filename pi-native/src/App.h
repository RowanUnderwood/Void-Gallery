#pragma once

#include <SDL.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "assets/AsyncLoader.h"
#include "assets/TextureStreamer.h"
#include "config/Config.h"
#include "modes/Mode.h"
#include "modes/MovingLights.h"
#include "render/Gl.h"
#include "render/Renderer.h"
#include "ui/Ui.h"
#include "util/FrameStats.h"
#include "util/Thermal.h"

namespace it {

class MazeMode;

struct AppOptions {
    std::string source = "http://localhost/";  // nginx base URL or local web-root directory
    std::string configSpec;                     // default: <source>/tunnel_config_potato.json
    std::string cacheDir;                       // default: ~/.cache/imagetunnel
    std::string configDir;                      // default: ~/.config/imagetunnel
    bool windowed = false;
    int width = 1920, height = 1080, refresh = 60;
    int rotation = -1;  // --rotate: monitor rotation (deg CW); overrides config when >= 0
    int frameCap = -1;  // --frame-cap: 30 | 60 overrides config; 0 = vsync off (diagnostic)
    int decodeThreads = 3;
    std::string benchMode;
    float benchSeconds = 120.0f;
    std::string benchCsv;
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
    void shutdown();

    void loadConfig(bool useOverride);
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
    void frame();

    float effectiveRenderScale() const;
    bool updateRandomMode(float dt, bool loading);
    int effectiveTextureEdge(int preferred) const;

    AppOptions opts_;
    std::string overridePath_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    int screenW_ = 0, screenH_ = 0;    // scan-out (window) size
    int logicalW_ = 1, logicalH_ = 1;  // virtual screen after monitor rotation
    float refreshHz_ = 60.0f;
    bool pace30_ = false;

    Config cfg_;
    std::shared_ptr<ImageSource> source_;
    std::unordered_map<std::string, std::string> manifest_;

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

    bool running_ = true;
    int exitCode_ = kExitRestart;
    bool benchStarted_ = false;
    bool benchPassed_ = true;
    float time_ = 0.0f;
    float readySince_ = -1.0f;
    uint64_t lastFrame_ = 0;
    uint64_t lastSwap_ = 0;
    bool cursorShown_ = true;
    bool relativeMouse_ = false;       // rotated monitor: own cursor from relative motion
    glm::vec2 cursor_{0.0f};           // mouse position in logical (rotated) pixels
    enum class RandomPhase { None, Out, In };
    RandomPhase randomPhase_ = RandomPhase::None;
    float randomFade_ = 0.0f;
    bool glDebug_ = false;  // IT_GL_DEBUG=1: report glGetError() every frame
};

}  // namespace it
