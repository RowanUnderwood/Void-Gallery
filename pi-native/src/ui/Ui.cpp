#include "ui/Ui.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>

#include "util/FrameStats.h"

namespace it {

namespace {

bool sliderD(const char* label, double& v, double lo, double hi, const char* fmt = "%.3f") {
    return ImGui::SliderScalar(label, ImGuiDataType_Double, &v, &lo, &hi, fmt);
}
bool sliderI(const char* label, int& v, int lo, int hi) { return ImGui::SliderInt(label, &v, lo, hi); }

// Slider that only reports a change once the user releases it (lil-gui's onFinishChange), used for
// settings that rebuild the scene.
bool sliderIFinish(const char* label, int& v, int lo, int hi) {
    ImGui::SliderInt(label, &v, lo, hi);
    return ImGui::IsItemDeactivatedAfterEdit();
}
bool sliderDFinish(const char* label, double& v, double lo, double hi, const char* fmt = "%.1f") {
    sliderD(label, v, lo, hi, fmt);
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool comboStr(const char* label, std::string& v, const std::vector<std::string>& items) {
    bool changed = false;
    if (ImGui::BeginCombo(label, v.c_str())) {
        for (const auto& it : items) {
            const bool sel = it == v;
            if (ImGui::Selectable(it.c_str(), sel)) {
                if (!sel) changed = true;
                v = it;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool comboInt(const char* label, int& v, const std::vector<int>& items) {
    bool changed = false;
    const std::string cur = std::to_string(v);
    if (ImGui::BeginCombo(label, cur.c_str())) {
        for (int it : items) {
            const bool sel = it == v;
            if (ImGui::Selectable(std::to_string(it).c_str(), sel)) {
                if (!sel) changed = true;
                v = it;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

}  // namespace

bool Ui::init(SDL_Window* window, SDL_GLContext ctx, int minScreenDim, const char* glslVersion) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // kiosk: no imgui.ini next to the binary
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // the app shows the cursor only with settings open
    ImGui::StyleColorsDark();
    const float k = std::max(1.0f, minScreenDim / 720.0f);
    ImGui::GetStyle().ScaleAllSizes(k);
    io.FontGlobalScale = k;
    if (!ImGui_ImplSDL2_InitForOpenGL(window, ctx)) return false;
    if (!ImGui_ImplOpenGL3_Init(glslVersion)) return false;
    initialized_ = true;
    return true;
}

void Ui::shutdown() {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

bool Ui::processEvent(const SDL_Event& e) {
    ImGui_ImplSDL2_ProcessEvent(&e);
    const ImGuiIO& io = ImGui::GetIO();
    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT) return io.WantCaptureKeyboard;
    if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL)
        return io.WantCaptureMouse;
    return false;
}

bool Ui::wantsKeyboard() const { return ImGui::GetIO().WantTextInput; }

void Ui::beginFrame(int logicalW, int logicalH) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    // UI is laid out on the logical (possibly portrait) screen; mouse events arrive pre-rotated.
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(logicalW), static_cast<float>(logicalH));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    ImGui::NewFrame();
}

void Ui::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Ui::toast(const std::string& msg, float seconds) {
    toast_ = msg;
    toastUntil_ = SDL_GetTicks64() / 1000.0 + seconds;
}

void Ui::setSoftwareCursor(bool on) {
    if (initialized_) ImGui::GetIO().MouseDrawCursor = on;
}

bool Ui::toastActive() const { return !toast_.empty() && SDL_GetTicks64() / 1000.0 < toastUntil_; }

void Ui::drawToast() {
    if (!toastActive()) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y - 60.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(toast_.c_str());
    ImGui::End();
}

void Ui::loadingOverlay(const std::string& title, const std::string& sub) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("##loading", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(title.c_str());
    ImGui::SetWindowFontScale(1.0f);
    if (!sub.empty()) ImGui::TextDisabled("%s", sub.c_str());
    ImGui::End();
}

void Ui::statsOverlay(const StatsInfo& s) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##stats", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    const FrameStats& f = *s.frames;
    ImGui::Text("%.1f fps   avg %.2f ms   p99 %.2f ms   cpu %.2f ms", f.fps(), f.avgMs(), f.percentileMs(0.99f),
                f.cpuMs());
    if (s.gpuMs > 0.0f) ImGui::Text("gpu %.2f ms per frame", s.gpuMs);
    ImGui::PlotLines("##ft", f.history(), static_cast<int>(f.count()), static_cast<int>(f.historyOffset()), nullptr,
                     0.0f, 50.0f, ImVec2(360, 50));
    ImGui::Text("draws %d   render %dx%d   auto-quality L%d", s.drawCalls, s.renderW, s.renderH, s.autoQualityLevel);
    ImGui::Text("textures %zu (%zu MB)   ready %zu   loading %zu   images %zu", s.texturesResident, s.textureMB,
                s.texturesReady, s.texturesInflight, s.sequenceSize);
    if (s.temperatureC >= 0) ImGui::Text("SoC %.1f C   throttled %s", s.temperatureC, s.throttled.c_str());
    ImGui::Text("source %s%s", s.source.c_str(), s.offline ? "  [OFFLINE - cache]" : "");
    if (!s.modeStatus.empty()) ImGui::TextUnformatted(s.modeStatus.c_str());
    if (!s.lastImage.empty()) ImGui::Text("last image: %s", s.lastImage.c_str());
    if (!s.lastError.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", s.lastError.c_str());
    ImGui::End();
}

UiActions Ui::settingsPanel(Config& cfg, float maxAniso, const PanelContext& pc) {
    UiActions a;
    if (!settingsVisible) return a;
    ModeSettings& m = cfg.cur();
    auto& g = cfg.globals;
    auto& pi = g.pi;
    const std::string& mode = cfg.activeMode;

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x - 20, 20), ImGuiCond_FirstUseEver, ImVec2(1, 0));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.34f, ds.y * 0.85f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    const char* title = pc.configMode ? "Image Tunnel Screensaver Settings" : "Settings  (H to hide)";
    if (!ImGui::Begin(title, pc.configMode ? nullptr : &settingsVisible)) {
        ImGui::End();
        return a;
    }
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 11.0f);  // leave room for the longest labels

    if (pc.configMode) {
        if (ImGui::Button("Save & Close")) a.saveAndClose = true;
        ImGui::SameLine();
        if (ImGui::Button("Save")) a.save = true;
        ImGui::SameLine();
        if (ImGui::Button("Close without saving")) a.closeNoSave = true;
        ImGui::Separator();
    }
    if (caps::kDesktop) {
        ImGui::InputTextWithHint("Image Source", "http://server:port/  or  C:\\path\\to\\web-root", &pi.source);
        if (ImGui::IsItemDeactivatedAfterEdit()) a.sourceChanged = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply")) a.sourceChanged = true;
    }

    // "random" is a fifth choice layered on the four real modes (kept in globals.pi so the web
    // viewer never sees an unknown activeMode).
    std::vector<std::string> modeItems = Config::modeNames();
    modeItems.push_back("random");
    std::string shown = pi.randomMode ? "random" : mode;
    if (comboStr("Display Mode", shown, modeItems)) {
        if (shown == "random") {
            pi.randomMode = true;
        } else {
            pi.randomMode = false;
            a.modeChanged = true;
            a.newMode = shown;
        }
    }
    if (pi.randomMode) {
        sliderD("Switch Every (s)", pi.randomInterval, 10, 600, "%.0f");
        ImGui::TextDisabled("Now showing: %s (its settings below)", mode.c_str());
    }

    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Performance", &g.showStats);
        if (caps::kDesktop) {
            ImGui::Checkbox("3D SBS Mode", &g.useStereo);
            if (g.useStereo) {
                sliderD("3D Separation", m.stereoEyeSep, 0.0, 2.0);
                sliderD("SBS FOV Widen", m.stereoFOVBoost, 0.0, 80.0, "%.0f");
            }
            if (pc.windowed && ImGui::Button("Toggle Fullscreen (F11)")) a.toggleFullscreen = true;
        }
        if (ImGui::Button("Save Config")) a.save = true;
        ImGui::SameLine();
        if (ImGui::Button("Reload from server (R)")) a.reloadServer = true;
        sliderD("Speed", m.cameraSpeed, 0.1, 4.0);
        sliderD("Fog Density", m.fogDensity, 0.0, 0.02, "%.4f");
    }

    if (mode != "maze" && ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (mode == "tunnel" || mode == "floating")
            a.rebuild |= sliderDFinish("Tunnel/Float Radius", m.tunnelRadius, 10, 100);
        a.rebuild |= sliderDFinish("Image Size", m.imageSize, 30, 90);
        if (mode == "tunnel") a.rebuild |= sliderIFinish("Tunnel Depth", m.tunnelRows, 1, caps::kMaxTunnelRows);
        if (mode == "grid") {
            a.rebuild |= sliderIFinish("Grid Width", m.gridCols, 2, caps::kMaxGridDim);
            a.rebuild |= sliderIFinish("Grid Height", m.gridRows, 2, caps::kMaxGridDim);
            a.rebuild |= sliderDFinish("Grid Spacing", m.gridSpacing, 100, 1000, "%.0f");
            sliderD("Path Randomness", m.pathRandomness, 0, 1);
            sliderI("Edge Buffer", m.gridEdgeBuffer, 0, 5);
        }
        if (mode == "floating") {
            a.resetReload |= sliderIFinish("Float Count", m.totalImages, 10, caps::kMaxFloating);
            // off = pass through (index.html); make way = slide apart / gentle bumps; physics = Jolt
            // rigid bodies (they spin from hits unless Stop Rotation is ticked).
            comboStr("Collisions", m.floatingCollisions, {"off", "makeway", "physics"});
        }
    }

    if (ImGui::CollapsingHeader("Images")) {
        if (comboStr("Resolution", g.quality, {"full", "half", "quarter"})) a.reloadSequence = true;
        const int maxA = std::max(1, std::min(caps::kMaxAnisotropy, static_cast<int>(maxAniso)));
        if (sliderI("Anisotropy (Sharpness)", m.anisotropyLevel, 1, maxA)) a.anisoChanged = true;
        if (mode == "tunnel" && comboInt("Image Rotation", m.tunnelRotation, {0, 90, 180, 270})) a.rebuild = true;
        if (mode == "floating") {
            sliderD("Spin Speed", m.rotationSpeed, 0, 5);
            ImGui::Checkbox("Stop Rotation", &m.noRotation);
        }
        sliderD("Opacity", cfg.opacity, 0, 1);
    }

    if (ImGui::CollapsingHeader("Server / Local Images")) {
        if (comboStr("Image Folder", g.serverPath, g.imageFolders)) {
            g.serverStart = 1;
            a.reloadSequence = true;
        }
        ImGui::InputInt("Start #", &g.serverStart);
        ImGui::InputInt("End #", &g.serverEnd);
        if (ImGui::Button("Reload Range")) a.reloadSequence = true;
        ImGui::Separator();
        ImGui::TextDisabled(caps::kDesktop ? "Local folder (or drag & drop images / a folder onto the window)"
                                           : "Local folder (replaces browser upload)");
        ImGui::InputText("Folder path", &pi.localFolder);
        if (ImGui::Checkbox("Use local folder", &pi.useLocalFolder)) a.reloadSequence = true;
        if ((pi.useLocalFolder || pc.hasCustomFiles) && ImGui::Button("Clear custom (back to server)")) {
            pi.useLocalFolder = false;
            a.clearCustom = true;
            a.reloadSequence = true;
        }
    }

    if (mode == "maze" && ImGui::CollapsingHeader("Maze", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Minimap", &m.showMazeMap);
        if (ImGui::SliderInt("Maze Size", &m.mazeComplexity, 11, caps::kMaxMazeComplexity)) {
            if (m.mazeComplexity % 2 == 0) m.mazeComplexity += 1;
        }
        a.rebuild |= ImGui::IsItemDeactivatedAfterEdit();
        a.rebuild |= sliderIFinish("Image Count", m.mazeImageCount, 5, caps::kMaxMazeImages);
        sliderD("Spotlight Size", m.mazeSpotlightAngle, 0.1, 3.14159 / 2);
        sliderD("Spotlight Height", m.mazeSpotlightHeight, 2.0, 15.0);
        if (caps::kDesktop) {
            if (comboStr("Shadows", pi.mazeShadows, {"off", "nearest", "all"})) pi.qualityPreset = "custom";
            if (pi.mazeShadows != "off" &&
                comboInt("Shadow Resolution", m.mazeShadowRes, {256, 512, 1024, 2048, 4096}))
                pi.qualityPreset = "custom";
        } else {
            comboStr("Shadows", pi.mazeShadows, {"off", "nearest"});
            if (pi.mazeShadows == "nearest") comboInt("Shadow Resolution", m.mazeShadowRes, {256, 512});
        }
        sliderD("Walking Speed", m.mazeWalkingSpeed, 0.2, 5.0);
        sliderD("Texture Tiling", m.mazeTextureTiling, 0.5, 8.0);
        comboStr("Nav Style", m.navStyle, {"win95", "modern"});
        a.materialsChanged |= comboStr("Wall Texture", m.mazeWallTexture, g.availableTextures);
        a.materialsChanged |= comboStr("Floor Texture", m.mazeFloorTexture, g.availableTextures);
        a.materialsChanged |= comboStr("Ceiling Texture", m.mazeCeilingTexture, g.availableTextures);
        if (caps::kDesktop) comboStr("Regen Transition", pi.regenTransition, {"fade", "flip", "cut"});
        else comboStr("Regen Transition", pi.regenTransition, {"fade", "cut"});
    }

    if (ImGui::CollapsingHeader("Lighting & Performance")) {
        if (mode != "maze") sliderD("Light Speed", m.lightSpeed, 0, 30);
        sliderD("Ambient Brightness", m.ambientIntensity, 0, 3);
        sliderI("Textures / Frame", m.maxUploadsPerFrame, 1, 20);
        if (mode != "maze") a.lightsChanged |= sliderIFinish("Light Count", m.lightCount, 0, caps::kMaxLightCount);
        sliderD("Light Intensity", m.lightIntensity, 0, 3000, "%.0f");
        a.lightsChanged |= comboStr("Light Color", m.lightColorMode, {"random", "white"});
    }

    if (ImGui::CollapsingHeader(caps::kDesktop ? "Display & Quality" : "Raspberry Pi",
                                pc.configMode ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        bool q = false;  // any quality control touched -> preset becomes "custom"
        if (caps::kDesktop) {
            if (comboStr("Quality Preset", pi.qualityPreset, {"low", "medium", "high", "ultra", "custom"}))
                a.presetChanged = true;
            const char* capLabels[] = {"Unlocked (VRR / no vsync)", "30 fps", "60 fps", "120 fps"};
            const int values[] = {0, 30, 60, 120};
            int idx = pi.frameCap == 0 ? 0 : pi.frameCap <= 30 ? 1 : pi.frameCap <= 60 ? 2 : 3;
            if (ImGui::Combo("Frame Cap", &idx, capLabels, 4)) {
                pi.frameCap = values[idx];
                a.frameCapChanged = true;
            }
            if (!pc.pacing.empty()) ImGui::TextDisabled("pacing: %s", pc.pacing.c_str());
        } else if (comboInt("Frame Cap", pi.frameCap, {60, 30})) {
            a.frameCapChanged = true;
        }
        comboInt("Monitor Rotation (CW)", pi.rotation, {0, 90, 180, 270});
        q |= sliderD("Render Scale", pi.renderScale, 0.5, caps::kMaxRenderScale, "%.2f");
        if (caps::kDesktop) q |= comboInt("MSAA", pi.msaa, {0, 2, 4, 8});
        else q |= comboInt("MSAA", pi.msaa, {0, 4});
        q |= sliderI("Lights per Draw", pi.maxLights, 1, caps::kMaxLightsPerDraw);
        q |= ImGui::Checkbox("Normal Maps (maze)", &pi.normalMaps);
        if (caps::kDesktop) q |= ImGui::Checkbox("Roughness Maps (maze)", &pi.roughnessMaps);
        const std::vector<int> edges = caps::kDesktop ? std::vector<int>{512, 1024, 2048, 4096}
                                                      : std::vector<int>{256, 512, 1024, 2048};
        if (comboInt("Max Texture Edge", pi.maxTextureEdge, edges)) {
            a.resetReload = true;
            q = true;
        }
        a.resetReload |= sliderIFinish("Texture Budget (MB)", pi.textureMemoryMB, 128, caps::kDesktop ? 8192 : 2048);
        int uploadMB = std::max(1, pi.uploadBytesPerFrame / (1024 * 1024));
        if (sliderI("Upload MB / Frame", uploadMB, 1, caps::kDesktop ? 128 : 32))
            pi.uploadBytesPerFrame = uploadMB * 1024 * 1024;
        ImGui::Checkbox("Auto Quality", &pi.autoQuality);
        if (q && caps::kDesktop) pi.qualityPreset = "custom";
    }

    ImGui::Separator();
    if (!caps::kDesktop) {
        if (ImGui::Button("Quit (stop kiosk)")) a.quit = true;  // exit 10: systemd leaves it stopped
    } else if (!pc.configMode) {
        if (ImGui::Button("Quit")) a.quit = true;
    }
    ImGui::PopItemWidth();
    ImGui::End();
    return a;
}

}  // namespace it
