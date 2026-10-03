#include "modes/MovingLights.h"

#include <algorithm>

#include "config/Config.h"
#include "render/Renderer.h"

namespace it {

void MovingLights::setup(const ModeSettings& s) {
    lights_.clear();
    const int count = std::clamp(s.lightCount, 0, caps::kMaxLightCount);
    const float radius = static_cast<float>(s.tunnelRadius);
    for (int i = 0; i < count; ++i) {
        Light l;
        l.srgb = s.lightColorMode == "random" ? hslToRgb(frand(), 1.0f, 0.5f) : glm::vec3(1.0f);
        l.linear = srgbToLinear(l.srgb);
        const float angle = frand() * 2.0f * kPi;
        const float r = frand() * radius * 0.8f;
        l.pos = {std::cos(angle) * r, std::sin(angle) * r, -frand() * 1000.0f};
        lights_.push_back(l);
    }
}

void MovingLights::update(float dt, const ModeSettings& s) {
    const float radius = static_cast<float>(s.tunnelRadius);
    for (auto& l : lights_) {
        l.pos.z += static_cast<float>(s.lightSpeed) * 1.5f * dt * 10.0f;
        if (l.pos.z > 100.0f) {
            l.pos = {(frand() * 2 - 1) * radius, (frand() * 2 - 1) * radius, -1000.0f - frand() * 200.0f};
        }
    }
}

void MovingLights::drawOrbs(Renderer& gfx, const Camera& cam, float fogDensity) const {
    for (const auto& l : lights_) {
        gfx.drawUnlit(gfx.sphere, glm::translate(glm::mat4(1.0f), l.pos), cam, glm::vec4(l.srgb, 1.0f), fogDensity);
    }
}

int MovingLights::nearest(const glm::vec3& p, int maxN, int* out) const {
    struct Cand { float d2; int i; };
    Cand c[caps::kMaxLightCount];
    int n = 0;
    for (int i = 0; i < static_cast<int>(lights_.size()) && n < caps::kMaxLightCount; ++i) {
        const glm::vec3 d = lights_[i].pos - p;
        const float d2 = glm::dot(d, d);
        if (d2 < kRange * kRange) c[n++] = {d2, i};
    }
    const int k = std::min(n, maxN);
    std::partial_sort(c, c + k, c + n, [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
    for (int i = 0; i < k; ++i) out[i] = c[i].i;
    return k;
}

}  // namespace it
