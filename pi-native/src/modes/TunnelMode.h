#pragma once

#include <vector>

#include "modes/CardBatch.h"
#include "modes/Mode.h"

namespace it {

class TunnelMode final : public Mode {
public:
    using Mode::Mode;
    const char* name() const override { return "tunnel"; }
    void enter() override;
    void exit() override;
    void update(float dt, float time) override;
    void render() override;
    size_t texturesNeeded() const override;
    int preferredTextureEdge() const override { return 512; }

private:
    struct Task {
        int col;
        float z;
    };
    struct Tile {
        TexRef tex;
        int col;
        float z;
        glm::mat3 uv;
    };
    void processSpawns();
    int columnsFor(float radius, float imageSize) const;

    gl::Mesh segment_;  // one shared segment for every tile
    int cols_ = 0, rows_ = 0;
    float rowHeight_ = 0, totalDepth_ = 0, segmentAngle_ = 0, cellWidth_ = 0;
    std::vector<Task> tasks_;
    std::vector<Tile> tiles_;
    CardBatch batch_;
};

}  // namespace it
