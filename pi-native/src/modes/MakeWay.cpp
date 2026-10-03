#include "modes/MakeWay.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace it {

int stepMakeWay(std::vector<FloatBody>& bodies, float dt, const MakeWayParams& p) {
    const size_t n = bodies.size();
    if (n == 0 || dt <= 0.0f) return 0;

    // 1. Own dynamics: ease back to cruise speed, damp sideways drift, stay inside the ring.
    const float relax = std::min(1.0f, p.speedRelax * dt);
    const float damp = std::exp(-p.lateralDamping * dt);
    for (auto& b : bodies) {
        b.vel.z += (b.cruise - b.vel.z) * relax;
        b.vel.x *= damp;
        b.vel.y *= damp;
        const float r = glm::length(glm::vec2(b.pos));
        if (r > 1e-3f) {
            const glm::vec2 dir = glm::vec2(b.pos) / r;
            float spring = 0.0f;
            if (r > p.radius * 1.1f) spring = -(r - p.radius * 1.1f);
            else if (r < p.radius * 0.5f) spring = (p.radius * 0.5f - r);
            b.vel.x += dir.x * spring * 2.0f * dt;
            b.vel.y += dir.y * spring * 2.0f * dt;
        }
    }

    // 2. Pairs, swept along z: only cards within (closing speed * lookahead + gap) can interact.
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return bodies[a].pos.z < bodies[b].pos.z; });
    float maxSpeed = 0.0f, minSpeed = 1e9f;
    for (const auto& b : bodies) {
        maxSpeed = std::max(maxSpeed, b.vel.z);
        minSpeed = std::min(minSpeed, b.vel.z);
    }
    const float window = std::max(0.0f, maxSpeed - minSpeed) * p.lookahead + p.gap + 1.0f;

    int contacts = 0;
    for (size_t ii = 0; ii < n; ++ii) {
        FloatBody& rear = bodies[order[ii]];
        for (size_t jj = ii + 1; jj < n; ++jj) {
            FloatBody& front = bodies[order[jj]];  // larger z = closer to the camera
            const float dz = front.pos.z - rear.pos.z;
            if (dz > window) break;
            const glm::vec2 cr = glm::vec2(rear.pos) + rear.offset;
            const glm::vec2 cf = glm::vec2(front.pos) + front.offset;
            const glm::vec2 d = cf - cr;
            const float ox = rear.half.x + front.half.x + p.padding - std::abs(d.x);
            const float oy = rear.half.y + front.half.y + p.padding - std::abs(d.y);
            if (ox <= 0.0f || oy <= 0.0f) continue;  // not overlapping on screen

            const float closing = rear.vel.z - front.vel.z;  // > 0: rear is catching up
            if (closing > 0.0f || dz < p.gap) {
                // Sideways "make way": stronger the sooner they would meet, along the axis that
                // needs the least movement to separate the footprints.
                const float t = closing > 1e-3f ? std::max(0.0f, dz - p.gap) / closing : 0.0f;
                const float urgency = std::clamp(1.0f - t / p.lookahead, 0.0f, 1.0f);
                if (urgency > 0.0f) {
                    glm::vec2 axis = ox < oy ? glm::vec2(d.x >= 0.0f ? 1.0f : -1.0f, 0.0f)
                                             : glm::vec2(0.0f, d.y >= 0.0f ? 1.0f : -1.0f);
                    if (d.x == 0.0f && d.y == 0.0f) axis = glm::vec2(1.0f, 0.0f);
                    const float a = p.push * urgency * dt;
                    front.vel.x += axis.x * a;
                    front.vel.y += axis.y * a;
                    rear.vel.x -= axis.x * a;
                    rear.vel.y -= axis.y * a;
                }
            }
            if (dz < p.gap) {
                ++contacts;
                if (closing > 0.0f) {  // bump: equal masses, momentum conserved, little bounce
                    const float avg = 0.5f * (rear.vel.z + front.vel.z);
                    rear.vel.z = avg - 0.5f * p.restitution * closing;
                    front.vel.z = avg + 0.5f * p.restitution * closing;
                }
                const float fix = 0.5f * (p.gap - dz);  // never let them cross
                rear.pos.z -= fix;
                front.pos.z += fix;
            }
        }
    }

    // 3. Integrate; afterwards no overlapping pair can have swapped order this step, because the
    //    bump removed the closing speed and the gap absorbs the remaining relative motion.
    for (auto& b : bodies) b.pos += b.vel * dt;
    return contacts;
}

}  // namespace it
