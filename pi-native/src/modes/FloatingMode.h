#pragma once

#include <memory>
#include <string>
#include <vector>

#include "modes/CardBatch.h"
#include "modes/FloatingPhysics.h"
#include "modes/MakeWay.h"
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
    std::string status() const override;

private:
    struct Task {
        glm::vec3 pos;
        glm::vec3 rot;
    };
    struct Card {
        TexRef tex;
        FloatBody body;        // position, velocity, footprint (make-way state)
        glm::vec2 size;        // quad size
        glm::vec3 rot;         // Euler spin (off / make-way), as index.html
        glm::vec3 rotSpeed;
        glm::quat orient{1, 0, 0, 0};  // physics orientation
        float velocity = 1.0f;         // per-card speed factor (index.html: 1.0 .. 1.5)
        uint32_t physId = UINT32_MAX;
    };
    void queueSpawn(bool randomZ);
    bool spawnSpotFree(const glm::vec3& p) const;
    void processSpawns();
    void removeCard(size_t i);
    std::string collisions() const;

    std::vector<Task> tasks_;
    std::vector<Card> cards_;
    std::vector<FloatBody> scratch_;
    std::unique_ptr<FloatingPhysics> physics_;
    std::string activeCollisions_;
    bool physicsSpin_ = false;
    int lastContacts_ = 0;
    float solverMs_ = 0.0f;
    CardBatch batch_;
};

}  // namespace it
