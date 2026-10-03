#pragma once
// The drifting point lights of floating / tunnel / grid mode (createMovingLight() in index.html).

#include <vector>

#include <glm/glm.hpp>

#include "render/MathUtil.h"

namespace it {

struct Renderer;
struct ModeSettings;

class MovingLights {
public:
    struct Light {
        glm::vec3 pos;
        glm::vec3 srgb;    // orb colour
        glm::vec3 linear;  // light colour
    };

    void setup(const ModeSettings& s);
    void clear() { lights_.clear(); }
    void update(float dt, const ModeSettings& s);
    void drawOrbs(Renderer& gfx, const Camera& cam, float fogDensity) const;

    // Indices of up to `maxN` lights nearest to `p` (within range); returns the count.
    int nearest(const glm::vec3& p, int maxN, int* out) const;

    const std::vector<Light>& lights() const { return lights_; }
    static constexpr float kRange = 600.0f;  // PointLight distance in index.html

private:
    std::vector<Light> lights_;
};

}  // namespace it
