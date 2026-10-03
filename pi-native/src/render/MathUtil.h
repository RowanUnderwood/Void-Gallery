#pragma once
// three.js-compatible math helpers (Euler orders, color management, perspective camera).

#include <algorithm>
#include <cmath>
#include <random>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace it {

constexpr float kPi = 3.14159265358979f;

inline glm::quat axisAngle(const glm::vec3& axis, float a) { return glm::angleAxis(a, axis); }

// three.js Euler 'XYZ' (default) -> quaternion (matrix = Rx * Ry * Rz).
inline glm::quat quatXYZ(float x, float y, float z) {
    return axisAngle({1, 0, 0}, x) * axisAngle({0, 1, 0}, y) * axisAngle({0, 0, 1}, z);
}
// three.js Euler 'YXZ' (used for the maze camera).
inline glm::quat quatYXZ(float x, float y, float z) {
    return axisAngle({0, 1, 0}, y) * axisAngle({1, 0, 0}, x) * axisAngle({0, 0, 1}, z);
}

inline float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline glm::vec3 srgbToLinear(const glm::vec3& c) { return {srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b)}; }
inline glm::vec3 hexColor(unsigned hex) {
    return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f};
}

// three.js Color.setHSL (returns sRGB components).
inline glm::vec3 hslToRgb(float h, float s, float l) {
    auto hue2rgb = [](float p, float q, float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f / 6) return p + (q - p) * 6 * t;
        if (t < 0.5f) return q;
        if (t < 2.0f / 3) return p + (q - p) * 6 * (2.0f / 3 - t);
        return p;
    };
    if (s == 0) return glm::vec3(l);
    const float q = l <= 0.5f ? l * (1 + s) : l + s - l * s;
    const float p = 2 * l - q;
    return {hue2rgb(p, q, h + 1.0f / 3), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.0f / 3)};
}

inline std::mt19937& rng() {
    static thread_local std::mt19937 gen{std::random_device{}()};
    return gen;
}
inline float frand() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

inline float smoothstep01(float t) { return t * t * (3 - 2 * t); }

struct Camera {
    glm::vec3 pos{0.0f};
    glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
    float fovDeg = 75.0f;
    float aspect = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ = 3000.0f;
    glm::mat4 clipRotation{1.0f};  // rotates the image for a turned monitor (see screenRotation())
    // 3D SBS (three.js StereoCamera): eye offset along the camera's X axis and the matching
    // off-axis frustum shift so both eyes converge at the focus distance.
    float eyeOffset = 0.0f;
    float projShiftX = 0.0f;

    glm::mat4 view() const {
        return glm::translate(glm::mat4(1.0f), glm::vec3(-eyeOffset, 0.0f, 0.0f)) * glm::mat4_cast(glm::conjugate(rot)) *
               glm::translate(glm::mat4(1.0f), -pos);
    }
    glm::mat4 proj() const {
        glm::mat4 p = glm::perspective(glm::radians(fovDeg), aspect, nearZ, farZ);
        p[2][0] += projShiftX;
        return clipRotation * p;
    }
    glm::vec3 forward() const { return rot * glm::vec3(0, 0, -1); }
    void translateZ(float d) { pos += rot * glm::vec3(0, 0, d); }
};

// Clip-space rotation that draws a logical (portrait) frame onto the landscape scan-out of a
// monitor turned `degreesCW` clockwise. Logical NDC (x, y) -> window NDC: 90 -> (-y, x),
// 180 -> (-x, -y), 270 -> (y, -x). Same mapping as kPresentVS, so both paths agree.
inline glm::mat4 screenRotation(int degreesCW) {
    glm::mat4 r(1.0f);
    if (degreesCW == 90) {
        r[0][0] = 0; r[1][0] = -1; r[0][1] = 1; r[1][1] = 0;
    } else if (degreesCW == 180) {
        r[0][0] = -1; r[1][1] = -1;
    } else if (degreesCW == 270) {
        r[0][0] = 0; r[1][0] = 1; r[0][1] = -1; r[1][1] = 0;
    }
    return r;
}

// Far plane where exp2 fog reaches ~99.8%: anything beyond is drawn as pure fog colour anyway.
inline float fogFarPlane(float density, float maxFar = 3000.0f) {
    if (density <= 0.0f) return maxFar;
    return std::min(maxFar, 2.5f / density);
}

}  // namespace it
