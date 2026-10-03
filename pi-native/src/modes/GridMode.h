#pragma once

#include <vector>

#include "modes/CardBatch.h"
#include "modes/Mode.h"

namespace it {

class GridMode final : public Mode {
public:
    using Mode::Mode;
    const char* name() const override { return "grid"; }
    void enter() override;
    void exit() override;
    void update(float dt, float time) override;
    void render() override;
    size_t texturesNeeded() const override;
    int preferredTextureEdge() const override { return 384; }
    std::string status() const override;

private:
    struct Tile {
        TexRef tex;
        glm::vec2 xy;
        glm::vec2 size;
    };
    struct Layer {
        float z = 0;
        std::vector<Tile> tiles;
    };
    struct Task {
        int layer;
        glm::vec2 xy;
    };
    void createTasks(int layer);
    void recycle(int layer);
    void pickNextTarget();
    glm::vec2 targetPosition(int c, int r) const;
    void processSpawns();

    std::vector<Layer> layers_;
    std::vector<Task> tasks_;
    glm::vec2 target_{0.0f};
    int targetC_ = 0, targetR_ = 0;
    CardBatch batch_;
};

}  // namespace it
