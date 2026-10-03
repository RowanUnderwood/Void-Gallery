#pragma once

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "assets/AsyncLoader.h"
#include "assets/TextureStreamer.h"
#include "config/Config.h"
#include "render/MathUtil.h"
#include "render/Renderer.h"

namespace it {

class MovingLights;

// Everything a mode needs from the app. Owned by App; modes keep a reference.
struct ModeContext {
    Config& cfg;
    TextureStreamer& textures;
    AsyncLoader& loader;
    std::shared_ptr<ImageSource>& source;
    Renderer& gfx;
    Camera& cam;
    MovingLights& lights;
    glm::vec2 mouse{0.0f};      // NDC, like index.html's `mouse`
    int screenW = 1920;
    int screenH = 1080;
    int maxLightsPerDraw = 4;   // after auto-quality
    bool normalMaps = true;     // after auto-quality
    uint64_t frameIndex = 0;    // increments once per frame (render() may run twice in 3D SBS)
};

class Mode {
public:
    explicit Mode(ModeContext& ctx) : ctx_(ctx) {}
    virtual ~Mode() = default;

    virtual const char* name() const = 0;
    virtual void enter() = 0;               // (re)build the scene; called on mode switch and rebuilds
    virtual void exit() {}                  // release scene objects
    virtual void update(float dt, float time) = 0;
    virtual void render() = 0;              // into the bound scene target
    virtual void renderOverlay() {}         // 2D, at native resolution

    virtual size_t texturesNeeded() const = 0;  // drives buffer size (preloadAndStart in index.html)
    virtual int preferredTextureEdge() const { return 1024; }
    virtual bool busy() const { return false; }      // shows the loading overlay
    virtual std::string status() const { return {}; }
    virtual bool usesMovingLights() const { return true; }
    // Horizontal squeeze of the whole frame, 1 = none (maze "flip" regeneration transition).
    virtual float frameSquish() const { return 1.0f; }

protected:
    ModeSettings& s() { return ctx_.cfg.cur(); }
    ModeContext& ctx_;
};

}  // namespace it
