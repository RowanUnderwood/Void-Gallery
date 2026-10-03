#pragma once
// Optional rigid-body physics for floating mode (Jolt Physics). Cards are thin boxes covering their
// visible pixels; off-centre hits make them spin (unless rotation is stopped, in which case only
// translation is simulated). The same gentle "conveyor" as make-way keeps them drifting toward the
// camera at their own cruise speed and inside the ring; collisions add the bumps and the spin.

#include <cstdint>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace it {

class FloatingPhysics {
public:
    struct Params {
        float radius = 85.0f;
        float speedRelax = 0.6f;
        float lateralDamping = 0.8f;
        float angularDamping = 0.6f;  // spin from bumps fades out over a few seconds
        float restitution = 0.2f;
        float friction = 0.3f;
    };

    explicit FloatingPhysics(bool allowSpin);
    ~FloatingPhysics();
    FloatingPhysics(const FloatingPhysics&) = delete;
    FloatingPhysics& operator=(const FloatingPhysics&) = delete;

    // halfSize/offset describe the visible footprint in the card's plane.
    uint32_t add(const glm::vec3& pos, const glm::quat& rot, const glm::vec2& halfSize, const glm::vec2& offset,
                 const glm::vec3& vel, const glm::vec3& angVel);
    void remove(uint32_t id);
    void setCruise(uint32_t id, float cruise);
    void step(float dt, const Params& p);
    void read(uint32_t id, glm::vec3& pos, glm::quat& rot) const;
    int bodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace it
