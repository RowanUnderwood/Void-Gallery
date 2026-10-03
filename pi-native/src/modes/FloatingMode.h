#pragma once

#include <vector>

#include "modes/CardBatch.h"
#include "modes/Mode.h"

namespace it {

class FloatingMode final : public Mode {
public:
    using Mode::Mode;
    const char* name() const override { return "floating"; }
    void enter() override;
    void exit() override;
    void update(float dt, float time) override;
    void render() override;
    size_t texturesNeeded() const override;
    int preferredTextureEdge() const override { return 1024; }

private:
    struct Task {
        glm::vec3 pos;
        glm::vec3 rot;
    };
    struct Card {
        TexRef tex;
        glm::vec3 pos;
        glm::vec2 size;
        glm::vec3 rot;
        glm::vec3 rotSpeed;
        float velocity;
    };
    void queueSpawn(bool randomZ);
    void processSpawns();

    std::vector<Task> tasks_;
    std::vector<Card> cards_;
    CardBatch batch_;
};

}  // namespace it
