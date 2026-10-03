#pragma once
// updateTileUVs() from index.html as a 3x3 UV transform: the web version rewrote every vertex's
// UVs on a cloned geometry per tile; the mapping is affine, so one matrix per tile is equivalent.

#include <cmath>

#include <glm/glm.hpp>

namespace it {

// Per-vertex mapping exactly as in updateTileUVs().
inline glm::vec2 tunnelUvMap(glm::vec2 uv, int rot, float sx, float sy) {
    float u = 1.0f - uv.x;
    float v = uv.y;
    if (rot == 90) {
        const float t = u;
        u = v;
        v = 1.0f - t;
    } else if (rot == 180) {
        u = 1.0f - u;
        v = 1.0f - v;
    } else if (rot == 270) {
        const float t = u;
        u = 1.0f - v;
        v = t;
    }
    return {u * sx + (1.0f - sx) / 2.0f, v * sy + (1.0f - sy) / 2.0f};
}

inline glm::mat3 tunnelUvTransform(float ratio, int colIndex, float segmentAngle, float cellWidth, float imageSize,
                                   int tunnelRotation, int* outRot = nullptr, float* outSx = nullptr,
                                   float* outSy = nullptr) {
    const float twoPi = 6.28318530717958f;
    const float pi = 3.14159265358979f;
    int baseRot = tunnelRotation;
    const float angle = std::fmod(colIndex * segmentAngle, twoPi);
    if (angle > pi / 2 && angle < 3 * pi / 2) baseRot += 180;  // keep images upright on the far side
    float eff = ratio;
    const int totalRot = baseRot % 360;
    const bool portrait = std::abs((totalRot % 180) - 90) < 1;
    if (portrait) eff = 1.0f / ratio;
    const float cellRatio = cellWidth / imageSize;
    float sx = 1.0f, sy = 1.0f;
    if (eff > cellRatio) sy = (cellWidth / eff) / imageSize;
    else sx = (imageSize * eff) / cellWidth;
    const int r = ((baseRot % 360) + 360) % 360;

    const glm::vec2 o = tunnelUvMap({0, 0}, r, sx, sy);
    const glm::vec2 a = tunnelUvMap({1, 0}, r, sx, sy);
    const glm::vec2 b = tunnelUvMap({0, 1}, r, sx, sy);
    if (outRot) *outRot = r;
    if (outSx) *outSx = sx;
    if (outSy) *outSy = sy;
    return glm::mat3(glm::vec3(a - o, 0.0f), glm::vec3(b - o, 0.0f), glm::vec3(o, 1.0f));
}

}  // namespace it
