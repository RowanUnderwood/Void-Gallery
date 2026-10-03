#pragma once

#include <array>
#include <random>
#include <string>
#include <vector>

#include "modes/MazeGen.h"
#include "modes/Mode.h"

namespace it {

class MazeMode final : public Mode {
public:
    explicit MazeMode(ModeContext& ctx);
    ~MazeMode() override;

    const char* name() const override { return "maze"; }
    void enter() override;
    void exit() override;
    void update(float dt, float time) override;
    void render() override;
    void renderOverlay() override;
    size_t texturesNeeded() const override;
    int preferredTextureEdge() const override { return 1024; }
    bool busy() const override;
    std::string status() const override;
    bool usesMovingLights() const override { return false; }

    void syncMaterials();               // (re)load wall/floor/ceiling textures if the config changed
    void reloadMaterials();             // force reload (e.g. image source changed)
    void setAnisotropy(int level);
    void resetSpotColors();             // light colour mode changed

private:
    enum Surface { kWall = 0, kFloor, kCeiling, kSurfaceCount };
    struct Material {
        std::string name;
        GLuint color = 0;
        GLuint normal = 0;
        int pending = 0;
        uint64_t token = 0;
    };
    struct Painting {
        TexRef tex;
        glm::vec3 pos;      // on the wall surface, painting centre height
        glm::vec3 normal;
        float rotY;
        float w, h;
    };
    struct Task {
        WallFace face;
        glm::vec3 worldPos;
    };
    struct Spot {
        int painting = -1;
        float level = 0.0f;  // fade-in 0..1
        glm::vec3 srgb{1.0f};
    };
    struct Nav {
        int row = 1, col = 1, trow = 1, tcol = 1;
        char facing = 'S';
        enum Phase { Idle, Moving, Turning } phase = Idle;
        float progress = 1.0f;
        float headBob = 0.0f;
        glm::vec3 startPos{0.0f}, endPos{0.0f};
        glm::quat startQ{1, 0, 0, 0}, endQ{1, 0, 0, 0};
    };
    enum class Regen { None, FadingOut, FadingIn };

    void requestMaterial(int surface, const std::string& name);
    void buildMaze();
    void clearMaze();
    void processSpawns();
    void rebuildFrames();
    void advanceNav();
    void startMoving();
    void updateNav(float dt);
    void updateSpots(float dt);
    void startRegen();
    void ensureShadowTarget(int size);
    void renderShadow(const glm::vec3& lightPos, const glm::vec3& target, const glm::vec3& up);
    glm::vec3 spotPosition(const Painting& p) const;
    glm::vec3 exitPosition() const;

    std::mt19937 rng_;
    MazeGrid grid_;
    bool built_ = false;
    Material mats_[kSurfaceCount];
    uint64_t matToken_ = 0;

    gl::Mesh walls_, floor_, ceiling_, frames_, mapWalls_, arrow_;
    std::vector<Painting> paintings_;
    std::vector<Task> tasks_;
    bool framesDirty_ = false;
    std::array<Spot, 4> spots_;

    int exitRow_ = 1, exitCol_ = 1;
    bool exitVisible_ = false;
    float exitSpinY_ = 0.0f, exitSpinZ_ = 0.0f, time_ = 0.0f;

    Nav nav_;
    Regen regen_ = Regen::None;
    float fade_ = 0.0f;

    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    int shadowSize_ = 0;
    int shadowSpot_ = -1;
    glm::mat4 shadowMat_{1.0f};
};

}  // namespace it
