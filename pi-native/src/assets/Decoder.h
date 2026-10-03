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
    std::vector<uint8_t> rgba;   // tightly packed RGBA8, bottom row first (GL / three.js flipY)
    size_t bytes() const { return rgba.size(); }
};

// Decodes WebP (libwebp, scaled during decode) or PNG/JPG (stb_image + box downscale) so the
// longest edge is at most `maxEdge` (0 = no limit).
bool decodeImage(const uint8_t* data, size_t size, int maxEdge, DecodedImage& out, std::string* err = nullptr);

// Area-average downscale of an RGBA8 image (exposed for tests).
void downscaleRgba(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh);

}  // namespace it
