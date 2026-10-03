#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "modes/FloatingMode.h"
#include "modes/GridMode.h"
#include "modes/MazeMode.h"
#include "modes/TunnelMode.h"

namespace fs = std::filesystem;

namespace it {

namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::string defaultDir(const char* xdgVar, const char* homeSub) {
    const std::string xdg = envOr(xdgVar, "");
    if (!xdg.empty()) return xdg + "/imagetunnel";
    return envOr("HOME", "/tmp") + "/" + homeSub + "/imagetunnel";
}

double seconds(uint64_t ticks) { return static_cast<double>(ticks) / SDL_GetPerformanceFrequency(); }

}  // namespace

// ============================================================================ startup / shutdown
int App::run(const AppOptions& opts) {
    opts_ = opts;
    if (opts_.cacheDir.empty()) opts_.cacheDir = defaultDir("XDG_CACHE_HOME", ".cache");
    if (opts_.configDir.empty()) opts_.configDir = defaultDir("XDG_CONFIG_HOME", ".config");
    overridePath_ = opts_.configDir + "/config.json";
    glDebug_ = !envOr("IT_GL_DEBUG", "").empty();

    source_ = makeImageSource(opts_.source, opts_.cacheDir);
    std::printf("[app] source %s, cache %s, config override %s\n", source_->describe().c_str(),
                opts_.cacheDir.c_str(), overridePath_.c_str());
    loadConfig(true);
    if (!opts_.benchMode.empty()) {
        cfg_.globals.pi.randomMode = false;  // benchmark exactly the requested mode
        if (cfg_.modes.count(opts_.benchMode)) cfg_.activeMode = opts_.benchMode;
        else std::fprintf(stderr, "[app] unknown bench mode '%s'\n", opts_.benchMode.c_str());
    }

    if (!initWindow()) return 1;

    gfx_ = std::make_unique<Renderer>();
    if (!gfx_->init()) {
        std::fprintf(stderr, "[app] shader setup failed\n");
        shutdown();
        return 1;
    }
    target_ = std::make_unique<gl::SceneTarget>();
    if (!ui_.init(window_, gl_, std::min(screenW_, screenH_))) {
        std::fprintf(stderr, "[app] ImGui init failed\n");
        shutdown();
        return 1;
    }

    loader_ = std::make_unique<AsyncLoader>(opts_.decodeThreads);
    streamer_ = std::make_unique<TextureStreamer>(*loader_);
    thermal_ = std::make_unique<ThermalMonitor>();
    ctx_.reset(new ModeContext{cfg_, *streamer_, *loader_, source_, *gfx_, cam_, lights_});
    modes_["floating"] = std::make_unique<FloatingMode>(*ctx_);
    modes_["tunnel"] = std::make_unique<TunnelMode>(*ctx_);
    modes_["grid"] = std::make_unique<GridMode>(*ctx_);
    auto maze = std::make_unique<MazeMode>(*ctx_);
    maze_ = maze.get();
    modes_["maze"] = std::move(maze);

    applySwapInterval();
    mode_ = modes_.at(cfg_.activeMode).get();
    loadSequence();  // builds the active mode once the image list is known
    SDL_ShowCursor(SDL_DISABLE);
    cursorShown_ = false;

    lastFrame_ = SDL_GetPerformanceCounter();
    while (running_) frame();

    if (!opts_.benchMode.empty()) {
        benchPassed_ = bench_.finish(opts_.benchCsv);
        exitCode_ = benchPassed_ ? 0 : kExitBenchFail;
    }
    shutdown();
    return exitCode_;
}

bool App::initWindow() {
    // KMSDRM only accepts swap interval 0/1. For a 30 fps cap, make SwapWindow block until the flip
    // completes so its return time marks the vblank that frame() paces from.
    if (cfg_.globals.pi.frameCap == 30) SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[app] SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    std::printf("[app] SDL video driver: %s\n", SDL_GetCurrentVideoDriver());
    const Uint32 flags = SDL_WINDOW_OPENGL | (opts_.windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN);
    const int minors[] = {1, 0};
    for (int minor : minors) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        window_ = SDL_CreateWindow("Image Tunnel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   opts_.windowed ? 1280 : opts_.width, opts_.windowed ? 720 : opts_.height, flags);
        if (!window_) break;
        if (!opts_.windowed) {
            // Pick the closest display mode to the requested resolution/refresh (default 1080p60).
            SDL_DisplayMode want{0, opts_.width, opts_.height, opts_.refresh, nullptr};
            SDL_DisplayMode got;
            if (SDL_GetClosestDisplayMode(0, &want, &got)) SDL_SetWindowDisplayMode(window_, &got);
        }
        gl_ = SDL_GL_CreateContext(window_);
        if (gl_) break;
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (!window_ || !gl_) {
        std::fprintf(stderr, "[app] could not create a GLES 3 context: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, gl_);
    SDL_GL_GetDrawableSize(window_, &screenW_, &screenH_);
    SDL_DisplayMode mode;
    if (SDL_GetWindowDisplayMode(window_, &mode) == 0 && mode.refresh_rate > 0) refreshHz_ = mode.refresh_rate;
    std::printf("[app] %s | %s | %dx%d @ %.0f Hz\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)), screenW_, screenH_, refreshHz_);
    return true;
}

void App::shutdown() {
    // GL objects must go before the context.
    modes_.clear();
    mode_ = nullptr;
    maze_ = nullptr;
    streamer_.reset();
    loader_.reset();  // joins workers; pending callbacks are dropped
    thermal_.reset();
    target_.reset();
    if (gfx_) gfx_->shutdown();
    gfx_.reset();
    ui_.shutdown();
    if (gl_) SDL_GL_DeleteContext(gl_);
    if (window_) SDL_DestroyWindow(window_);
    gl_ = nullptr;
    window_ = nullptr;
    SDL_Quit();
}

// ============================================================================ config
bool App::fetchJson(const std::string& spec, json& out) {
    std::vector<uint8_t> bytes;
    std::string err;
    bool ok = false;
    if (spec.rfind("http://", 0) == 0 || spec.rfind("https://", 0) == 0) {
        const auto slash = spec.find_last_of('/');
        auto src = makeImageSource(spec.substr(0, slash + 1), opts_.cacheDir);
        ok = src->fetch(spec.substr(slash + 1), bytes, &err, true);
    } else if (!spec.empty() && spec[0] == '/') {
        ok = readFile(spec, bytes);
        if (!ok) err = "cannot read " + spec;
    } else {
        ok = source_->fetch(spec, bytes, &err, true);
    }
    if (!ok) {
        std::fprintf(stderr, "[config] %s\n", err.c_str());
        return false;
    }
    try {
        out = json::parse(bytes.begin(), bytes.end());
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[config] %s: %s\n", spec.c_str(), e.what());
        return false;
    }
}

void App::loadConfig(bool useOverride) {
    Config c;
    json base;
    const std::string spec = opts_.configSpec.empty() ? "tunnel_config_potato.json" : opts_.configSpec;
    if (fetchJson(spec, base)) c.apply(base);
    else std::fprintf(stderr, "[config] base config unavailable, using built-in defaults\n");
    const auto availableTextures = c.globals.availableTextures;  // server-managed inventory

    if (useOverride) {
        std::vector<uint8_t> bytes;
        if (readFile(overridePath_, bytes)) {
            try {
                const json local = json::parse(bytes.begin(), bytes.end());
                const double v = local.value("version", 0.0);
                if (std::abs(v - kConfigVersion) < 1e-9) {
                    c.apply(local);
                    std::printf("[config] applied local override %s\n", overridePath_.c_str());
                } else {
                    std::fprintf(stderr, "[config] local override is version %.1f, ignoring\n", v);
                }
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[config] local override corrupt, ignoring: %s\n", e.what());
            }
        }
    }
    c.globals.availableTextures = availableTextures;
    if (opts_.rotation >= 0) c.globals.pi.rotation = opts_.rotation;  // device-specific: from the CLI
    if (opts_.frameCap > 0) c.globals.pi.frameCap = opts_.frameCap;
    c.clampForPi();
    cfg_ = std::move(c);
}

void App::saveConfig() {
    const std::string text = cfg_.toJson().dump(2);
    if (writeFileAtomic(overridePath_, text.data(), text.size())) ui_.toast("Settings saved to " + overridePath_);
    else ui_.toast("Could not save settings to " + overridePath_);
}

void App::reloadFromServer() {
    std::error_code ec;
    if (fs::exists(overridePath_, ec)) fs::rename(overridePath_, overridePath_ + ".bak", ec);
    loadConfig(false);
    if (maze_) maze_->reloadMaterials();
    if (mode_) mode_->exit();
    mode_ = modes_.at(cfg_.activeMode).get();
    applySwapInterval();
    loadSequence();  // rebuilds the mode
    ui_.toast("Reloaded server config (local override moved to config.json.bak)");
}

// ============================================================================ images
void App::loadSequence() {
    auto& g = cfg_.globals;
    std::vector<std::string> keys;
    manifest_.clear();
    if (g.pi.useLocalFolder && !g.pi.localFolder.empty()) {
        keys = listImageFiles(g.pi.localFolder);
        if (keys.empty()) {
            ui_.toast("No images in " + g.pi.localFolder + ", using server images");
            g.pi.useLocalFolder = false;
        }
    }
    if (keys.empty()) {
        json folderCfg;
        if (fetchJson(g.serverPath + "/config.json", folderCfg)) {
            const int total = folderCfg.value("totalImages", 0);
            if (total > 0) g.serverEnd = total;
        }
        json manifest;
        if (fetchJson(g.serverPath + "/manifest.json", manifest) && manifest.is_object()) {
            for (auto it = manifest.begin(); it != manifest.end(); ++it)
                if (it->is_string()) manifest_[it.key()] = it->get<std::string>();
        }
        const std::string folder = g.serverPath + "/" + qualityFolder(g.quality);
        for (int i = std::max(1, g.serverStart); i <= g.serverEnd; ++i)
            keys.push_back(folder + std::to_string(i) + ".webp");
    }
    streamer_->setSequence(source_, std::move(keys));
    rebuildMode();
}

int App::effectiveTextureEdge(int preferred) const {
    int edge = std::min(preferred, cfg_.globals.pi.maxTextureEdge);
    if (autoQ_.level() >= 4) edge = std::max(256, edge / 2);
    return edge;
}

void App::configureStreamer() {
    const size_t needed = std::max<size_t>(1, mode_->texturesNeeded());
    const size_t buffer = std::clamp<size_t>(needed / 3, 12, 48);
    int edge = effectiveTextureEdge(mode_->preferredTextureEdge());
    // Shrink the decode size until the worst case (all visible + buffered at full size) fits the budget.
    const double budget = cfg_.globals.pi.textureMemoryMB * 1024.0 * 1024.0;
    auto estimate = [&](int e) { return static_cast<double>(needed + buffer) * e * e * 4.0 * (4.0 / 3.0) * 0.75; };
    while (edge > 192 && estimate(edge) > budget) edge = edge * 3 / 4;
    streamer_->configure(buffer, std::min<size_t>(needed, 10), edge);
    streamer_->setAnisotropy(cfg_.cur().anisotropyLevel);
}

// ============================================================================ modes
void App::switchMode(const std::string& name) {
    if (!modes_.count(name)) return;
    if (mode_) mode_->exit();
    cfg_.activeMode = name;
    mode_ = modes_.at(name).get();
    if (maze_) maze_->setAnisotropy(cfg_.cur().anisotropyLevel);
    rebuildMode();
}

void App::rebuildMode() {
    if (!mode_) return;
    mode_->exit();
    configureStreamer();
    if (mode_->usesMovingLights()) lights_.setup(cfg_.cur());
    else lights_.clear();
    mode_->enter();
    readySince_ = -1.0f;
}

void App::applySwapInterval() {
    // Swap interval 2 is not reliable: SDL 2.32's KMSDRM backend accepts it but still flips every
    // vblank. So a 30 fps cap always uses interval 1 and paces swaps to every 2nd vblank in frame().
    if (opts_.frameCap == 0) {  // diagnostic: --frame-cap 0 disables vsync to measure raw render time
        SDL_GL_SetSwapInterval(0);
        pace30_ = false;
        std::printf("[app] vsync: OFF (diagnostic)\n");
        return;
    }
    SDL_GL_SetSwapInterval(1);
    pace30_ = cfg_.globals.pi.frameCap == 30 && refreshHz_ > 45.0f;
    std::printf("[app] vsync: %s\n", pace30_ ? "30 fps (every 2nd vblank)" : "every vblank");
}

void App::applyActions(const UiActions& a) {
    if (a.quit) {
        running_ = false;
        exitCode_ = kExitStop;
    }
    if (a.save) saveConfig();
    if (a.reloadServer) {
        reloadFromServer();
        return;
    }
    if (a.modeChanged) {
        switchMode(a.newMode);
        return;
    }
    if (a.frameCapChanged) applySwapInterval();
    if (a.anisoChanged) {
        streamer_->setAnisotropy(cfg_.cur().anisotropyLevel);
        if (maze_) maze_->setAnisotropy(cfg_.cur().anisotropyLevel);
    }
    if (a.materialsChanged && maze_) maze_->syncMaterials();
    if (a.lightsChanged) {
        if (mode_->usesMovingLights()) lights_.setup(cfg_.cur());
        if (maze_) maze_->resetSpotColors();
    }
    if (a.reloadSequence) {
        loadSequence();
        return;
    }
    if (a.resetReload) {
        streamer_->clearBuffer();
        rebuildMode();
        return;
    }
    if (a.rebuild) rebuildMode();
}

// ============================================================================ frame loop
void App::handleEvents() {
    SDL_Event e;
    int winW = 1, winH = 1;
    SDL_GetWindowSize(window_, &winW, &winH);
    while (SDL_PollEvent(&e)) {
        // Rotated monitor: the mouse still reports motion in the user's frame (right = right on the
        // physical desk), but SDL's cursor lives in the unrotated scan-out. So track our own cursor
        // in logical (rotated) coordinates from relative motion and hand that to ImGui and modes.
        if (e.type == SDL_MOUSEMOTION) {
            if (relativeMouse_) {
                cursor_ = glm::clamp(cursor_ + glm::vec2(e.motion.xrel, e.motion.yrel), glm::vec2(0.0f),
                                     glm::vec2(logicalW_ - 1, logicalH_ - 1));
            } else {
                cursor_ = glm::vec2(e.motion.x, e.motion.y) * glm::vec2(screenW_, screenH_) /
                          glm::vec2(std::max(1, winW), std::max(1, winH));
            }
            e.motion.x = static_cast<Sint32>(cursor_.x);
            e.motion.y = static_cast<Sint32>(cursor_.y);
        }
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            e.button.x = static_cast<Sint32>(cursor_.x);
            e.button.y = static_cast<Sint32>(cursor_.y);
        }
        const bool uiWants = ui_.processEvent(e);
        switch (e.type) {
            case SDL_QUIT:
                running_ = false;
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) SDL_GL_GetDrawableSize(window_, &screenW_, &screenH_);
                break;
            case SDL_MOUSEMOTION:
                ctx_->mouse = glm::clamp(glm::vec2((cursor_.x / logicalW_) * 2.0f - 1.0f,
                                                   -(cursor_.y / logicalH_) * 2.0f + 1.0f),
                                         glm::vec2(-1.0f), glm::vec2(1.0f));
                break;
            case SDL_KEYDOWN: {
                if (uiWants && ui_.wantsKeyboard()) break;
                const SDL_Keycode k = e.key.keysym.sym;
                const bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
                if (k == SDLK_h) ui_.settingsVisible = !ui_.settingsVisible;
                else if (k == SDLK_p) cfg_.globals.showStats = !cfg_.globals.showStats;
                else if (k == SDLK_r && !ctrl) reloadFromServer();
                else if (k == SDLK_q && ctrl) {
                    running_ = false;
                    exitCode_ = kExitStop;
                } else if (k == SDLK_ESCAPE) {
                    running_ = false;
                    exitCode_ = kExitRestart;
                }
                break;
            }
            default:
                break;
        }
    }
}

// Random mode: after `randomInterval` seconds of a loaded mode, fade to black, switch to a different
// mode, and fade back in once it has loaded. Returns true if the mode was switched this frame.
bool App::updateRandomMode(float dt, bool loading) {
    constexpr float kFade = 0.6f;
    const auto& pi = cfg_.globals.pi;
    if (randomPhase_ == RandomPhase::None) {
        if (pi.randomMode && !loading && readySince_ >= 0 && time_ - readySince_ >= pi.randomInterval)
            randomPhase_ = RandomPhase::Out;
        return false;
    }
    if (randomPhase_ == RandomPhase::Out) {
        randomFade_ = std::min(1.0f, randomFade_ + dt / kFade);
        if (randomFade_ < 1.0f) return false;
        std::vector<std::string> others;
        for (const auto& n : Config::modeNames())
            if (n != cfg_.activeMode) others.push_back(n);
        const std::string next = others[std::uniform_int_distribution<size_t>(0, others.size() - 1)(rng())];
        std::printf("[app] random: %s -> %s\n", cfg_.activeMode.c_str(), next.c_str());
        switchMode(next);
        randomPhase_ = RandomPhase::In;
        return true;
    }
    if (!loading) {  // RandomPhase::In: hold black until the new mode is ready
        randomFade_ = std::max(0.0f, randomFade_ - dt / kFade);
        if (randomFade_ <= 0.0f) randomPhase_ = RandomPhase::None;
    }
    return false;
}

float App::effectiveRenderScale() const {
    float s = static_cast<float>(cfg_.globals.pi.renderScale);
    if (autoQ_.level() >= 1) s = std::min(s, 0.85f);
    if (autoQ_.level() >= 2) s = std::min(s, 0.75f);
    return s;
}

void App::frame() {
    const uint64_t start = SDL_GetPerformanceCounter();
    const float frameMs = static_cast<float>(seconds(start - lastFrame_) * 1000.0);
    lastFrame_ = start;
    const float dt = std::clamp(frameMs / 1000.0f, 0.0f, 0.1f);
    time_ += dt;

    // Logical (rotated) size must be known before events: mouse coordinates are normalised by it.
    int rotation = cfg_.globals.pi.rotation;
    logicalW_ = std::max(1, (rotation == 90 || rotation == 270) ? screenH_ : screenW_);
    logicalH_ = std::max(1, (rotation == 90 || rotation == 270) ? screenW_ : screenH_);

    const bool wantRelative = rotation != 0;
    if (wantRelative != relativeMouse_) {
        relativeMouse_ = wantRelative;
        SDL_SetRelativeMouseMode(wantRelative ? SDL_TRUE : SDL_FALSE);
        cursor_ = glm::vec2(logicalW_, logicalH_) * 0.5f;
        cursorShown_ = !wantRelative;  // force the visibility update below
    }

    handleEvents();
    if (!running_) return;
    if (rotation != cfg_.globals.pi.rotation) return;  // changed via the settings panel: next frame

    loader_->poll();
    const ModeSettings& m = cfg_.cur();
    const auto& pi = cfg_.globals.pi;
    streamer_->update(static_cast<size_t>(pi.uploadBytesPerFrame), m.maxUploadsPerFrame);

    ctx_->screenW = logicalW_;
    ctx_->screenH = logicalH_;
    ctx_->maxLightsPerDraw = autoQ_.level() >= 4 ? std::min(2, pi.maxLights) : pi.maxLights;
    ctx_->normalMaps = autoQ_.level() < 3;
    cam_.aspect = static_cast<float>(logicalW_) / std::max(1, logicalH_);
    // index.html's 75° vertical FOV was designed for landscape, where vertical is the short axis.
    // In portrait keep 75° across the short (horizontal) axis instead, so the framing matches the
    // web version and the taller screen shows more, rather than cropping the sides to ~47°.
    cam_.fovDeg = cam_.aspect >= 1.0f
                      ? 75.0f
                      : glm::degrees(2.0f * std::atan(std::tan(glm::radians(37.5f)) / cam_.aspect));
    cam_.farZ = fogFarPlane(static_cast<float>(m.fogDensity));

    mode_->update(dt, time_);

    bool loading = !streamer_->preloaded() || mode_->busy();
    if (!loading && readySince_ < 0) readySince_ = time_;
    if (updateRandomMode(dt, loading)) loading = !streamer_->preloaded() || mode_->busy();
    const bool needUi = ui_.settingsVisible || cfg_.globals.showStats || loading || ui_.toastActive();

    // Rotated monitor: normally the rotation is folded into the projection (no extra pass). ImGui
    // can't be rotated that way, so while it is on screen the frame is composed in a portrait
    // buffer and copied rotated (costs one full-screen pass).
    const bool composeRotated = rotation != 0 && needUi;
    const glm::mat4 clipRot = (rotation != 0 && !composeRotated) ? screenRotation(rotation) : glm::mat4(1.0f);
    cam_.clipRotation = clipRot;
    gfx_->overlayRotation = clipRot;

    // ---- 3D scene (possibly scaled / multisampled)
    gfx_->drawCalls = 0;
    if (composeRotated) target_->begin(logicalW_, logicalH_, effectiveRenderScale(), pi.msaa, true);
    else target_->begin(screenW_, screenH_, effectiveRenderScale(), pi.msaa, false);
    glClearColor(0, 0, 0, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    mode_->render();
    glDisable(GL_CULL_FACE);
    target_->endScene();  // the overlay surface (window, or the rotation buffer) is now bound

    // ---- 2D overlays at native resolution
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    mode_->renderOverlay();
    if (randomFade_ > 0.0f)
        gfx_->drawRect2D(0, 0, static_cast<float>(logicalW_), static_cast<float>(logicalH_), {0, 0, 0, randomFade_},
                         logicalW_, logicalH_);

    if (needUi) {
        ui_.beginFrame(logicalW_, logicalH_);
        if (loading) {
            std::string sub;
            if (streamer_->sequenceSize() == 0) sub = "No images found. " + streamer_->lastError();
            else if (mode_->busy()) sub = "Building maze...";
            else
                sub = std::to_string(std::min(streamer_->ready(), streamer_->preloadTarget())) + " / " +
                      std::to_string(streamer_->preloadTarget());
            ui_.loadingOverlay("Loading...", sub);
        }
        if (cfg_.globals.showStats) {
            StatsInfo info;
            info.frames = &stats_;
            info.drawCalls = gfx_->drawCalls;
            info.texturesResident = TextureStreamer::residentCount();
            info.textureMB = TextureStreamer::residentBytes() / (1024 * 1024);
            info.texturesReady = streamer_->ready();
            info.texturesInflight = streamer_->inflight();
            info.sequenceSize = streamer_->sequenceSize();
            info.temperatureC = thermal_->temperatureC();
            info.throttled = thermal_->throttled();
            info.autoQualityLevel = autoQ_.level();
            info.source = source_->describe();
            info.offline = source_->isOffline();
            info.modeStatus = mode_->status();
            if (cfg_.globals.pi.randomMode && readySince_ >= 0) {
                const int left = static_cast<int>(cfg_.globals.pi.randomInterval - (time_ - readySince_));
                info.modeStatus = "random: " + cfg_.activeMode + ", next in " + std::to_string(std::max(0, left)) +
                                  " s\n" + info.modeStatus;
            }
            info.lastError = streamer_->lastError();
            const std::string& key = streamer_->lastTakenKey();
            const std::string file = key.substr(key.find_last_of('/') + 1);
            auto it = manifest_.find(file);
            info.lastImage = it != manifest_.end() ? it->second : file;
            info.renderW = target_->width();
            info.renderH = target_->height();
            ui_.statsOverlay(info);
        }
        const UiActions actions = ui_.settingsPanel(cfg_, gl::maxAnisotropy());
        ui_.drawToast();
        ui_.endFrame();
        applyActions(actions);
    }
    glDisable(GL_BLEND);

    // Pointer only while the settings panel is open. Rotated: SDL's cursor can't follow the
    // rotation, so ImGui draws it (inside the rotated UI) instead.
    ui_.setSoftwareCursor(relativeMouse_ && ui_.settingsVisible);
    const bool sdlCursor = ui_.settingsVisible && !relativeMouse_;
    if (sdlCursor != cursorShown_) {
        SDL_ShowCursor(sdlCursor ? SDL_ENABLE : SDL_DISABLE);
        cursorShown_ = sdlCursor;
    }

    if (glDebug_) {
        for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError())
            std::fprintf(stderr, "[gl] error 0x%04x in mode %s\n", err, cfg_.activeMode.c_str());
    }

    if (composeRotated) {  // draw the composed portrait frame onto the landscape scan-out, rotated
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenW_, screenH_);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        gfx_->drawRotated(target_->presentTexture(), rotation);
    }

    const float cpuMs = static_cast<float>(seconds(SDL_GetPerformanceCounter() - start) * 1000.0);
    if (pace30_) {
        // Flip on every 2nd vblank: the previous swap returned at a vblank (blocking flips), so a
        // swap issued after the next vblank lands on the one after it. Flush first so the GPU
        // renders while we wait; otherwise Mesa submits at swap time and the GPU only gets the
        // ~15 ms left before the target vblank.
        glFlush();
        const double earliest = 1.0 / refreshHz_ + 0.002;
        while (seconds(SDL_GetPerformanceCounter() - lastSwap_) < earliest) SDL_Delay(1);
    }
    SDL_GL_SwapWindow(window_);
    lastSwap_ = SDL_GetPerformanceCounter();

    if (frameMs > 0.0f && frameMs < 1000.0f) stats_.push(frameMs, cpuMs);

    // Auto-quality (off by default).
    const float budgetMs = 1000.0f / static_cast<float>(cfg_.globals.pi.frameCap);
    if (cfg_.globals.pi.autoQuality && !loading && stats_.count() >= FrameStats::kHistory) {
        if (autoQ_.update(dt, stats_.percentileMs(0.95f), budgetMs)) {
            ui_.toast("Auto quality level " + std::to_string(autoQ_.level()));
            const int edge = effectiveTextureEdge(mode_->preferredTextureEdge());
            if (edge != streamer_->maxEdge()) rebuildMode();
        }
    } else if (!cfg_.globals.pi.autoQuality && autoQ_.level() != 0) {
        autoQ_.reset();
    }

    // Benchmark: start after the scene has been ready for 2 s.
    if (!opts_.benchMode.empty()) {
        if (!benchStarted_ && readySince_ >= 0 && time_ - readySince_ > 2.0f) {
            const float targetHz = cfg_.globals.pi.frameCap == 30 ? std::min(30.0f, refreshHz_) : refreshHz_;
            bench_.start(cfg_.activeMode, opts_.benchSeconds, targetHz);
            benchStarted_ = true;
            std::printf("[bench] recording %s for %.0f s\n", cfg_.activeMode.c_str(), opts_.benchSeconds);
        } else if (benchStarted_ &&
                   !bench_.record(frameMs, cpuMs, TextureStreamer::residentBytes() / (1024 * 1024),
                                  thermal_->throttled())) {
            running_ = false;
        }
    }
}

}  // namespace it
