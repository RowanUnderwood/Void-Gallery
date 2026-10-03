#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace it {

struct DecodedImage {
    int width = 0;
    int height = 0;
    bool hasAlpha = false;       // true only if some pixel is actually translucent
    // Bounds of the visible (alpha >= 32) pixels as fractions of the image, GL convention
    // (v = 0 at the bottom). {0,0,1,1} for opaque images. Used as the collision footprint.
    float opaqueU0 = 0.0f, opaqueV0 = 0.0f, opaqueU1 = 1.0f, opaqueV1 = 1.0f;
    std::vector<uint8_t> rgba;   // tightly packed RGBA8, bottom row first (GL / three.js flipY)
    size_t bytes() const { return rgba.size(); }
};

// Decodes WebP (libwebp, scaled during decode) or PNG/JPG (stb_image + box downscale) so the
// longest edge is at most `maxEdge` (0 = no limit).
bool decodeImage(const uint8_t* data, size_t size, int maxEdge, DecodedImage& out, std::string* err = nullptr);

// Area-average downscale of an RGBA8 image (exposed for tests).
void downscaleRgba(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh);

}  // namespace it
