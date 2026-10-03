#include "assets/Decoder.h"

#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

namespace it {

namespace {

void fitWithin(int w, int h, int maxEdge, int& tw, int& th) {
    tw = w;
    th = h;
    if (maxEdge <= 0 || std::max(w, h) <= maxEdge) return;
    const double s = static_cast<double>(maxEdge) / std::max(w, h);
    tw = std::max(1, static_cast<int>(std::lround(w * s)));
    th = std::max(1, static_cast<int>(std::lround(h * s)));
}

bool scanAlpha(const std::vector<uint8_t>& rgba) {
    for (size_t i = 3; i < rgba.size(); i += 4)
        if (rgba[i] < 250) return true;
    return false;
}

// Tight bounds of the visible pixels (rows are already bottom-first, so v is GL-style).
void opaqueBounds(DecodedImage& img) {
    if (!img.hasAlpha) return;
    int x0 = img.width, y0 = img.height, x1 = -1, y1 = -1;
    for (int y = 0; y < img.height; ++y) {
        const uint8_t* row = img.rgba.data() + static_cast<size_t>(y) * img.width * 4;
        for (int x = 0; x < img.width; ++x) {
            if (row[x * 4 + 3] < 32) continue;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
    }
    if (x1 < 0) return;  // fully transparent: keep the whole rectangle
    img.opaqueU0 = static_cast<float>(x0) / img.width;
    img.opaqueU1 = static_cast<float>(x1 + 1) / img.width;
    img.opaqueV0 = static_cast<float>(y0) / img.height;
    img.opaqueV1 = static_cast<float>(y1 + 1) / img.height;
}

// GL samples row 0 as the bottom of a texture, while decoders emit the top row first. three.js
// flips every texture on upload (Texture.flipY = true); doing the same here keeps all UV math
// (tunnel tiles, normal maps) identical to index.html.
void flipRows(std::vector<uint8_t>& rgba, int w, int h) {
    const size_t stride = static_cast<size_t>(w) * 4;
    std::vector<uint8_t> tmp(stride);
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* a = rgba.data() + y * stride;
        uint8_t* b = rgba.data() + (h - 1 - y) * stride;
        std::memcpy(tmp.data(), a, stride);
        std::memcpy(a, b, stride);
        std::memcpy(b, tmp.data(), stride);
    }
}

bool isWebp(const uint8_t* d, size_t n) {
    return n >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0;
}

}  // namespace

void downscaleRgba(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    const double sx = static_cast<double>(sw) / dw;
    const double sy = static_cast<double>(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        const int y0 = static_cast<int>(y * sy);
        const int y1 = std::max(y0 + 1, std::min(sh, static_cast<int>(std::ceil((y + 1) * sy))));
        for (int x = 0; x < dw; ++x) {
            const int x0 = static_cast<int>(x * sx);
            const int x1 = std::max(x0 + 1, std::min(sw, static_cast<int>(std::ceil((x + 1) * sx))));
            uint32_t acc[4] = {0, 0, 0, 0};
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t* row = src + (static_cast<size_t>(yy) * sw + x0) * 4;
                for (int xx = x0; xx < x1; ++xx, row += 4) {
                    acc[0] += row[0];
                    acc[1] += row[1];
                    acc[2] += row[2];
                    acc[3] += row[3];
                }
            }
            const uint32_t n = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            uint8_t* o = dst + (static_cast<size_t>(y) * dw + x) * 4;
            for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>((acc[c] + n / 2) / n);
        }
    }
}

bool decodeImage(const uint8_t* data, size_t size, int maxEdge, DecodedImage& out, std::string* err) {
    out = DecodedImage{};
    if (!data || size == 0) {
        if (err) *err = "empty file";
        return false;
    }

    if (isWebp(data, size)) {
        WebPDecoderConfig cfg;
        if (!WebPInitDecoderConfig(&cfg)) {
            if (err) *err = "libwebp version mismatch";
            return false;
        }
        if (WebPGetFeatures(data, size, &cfg.input) != VP8_STATUS_OK) {
            if (err) *err = "bad webp header";
            return false;
        }
        int tw, th;
        fitWithin(cfg.input.width, cfg.input.height, maxEdge, tw, th);
        if (tw != cfg.input.width || th != cfg.input.height) {
            cfg.options.use_scaling = 1;
            cfg.options.scaled_width = tw;
            cfg.options.scaled_height = th;
        }
        out.width = tw;
        out.height = th;
        out.rgba.resize(static_cast<size_t>(tw) * th * 4);
        cfg.options.flip = 1;  // bottom row first, as GL expects (see flipRows)
        cfg.output.colorspace = MODE_RGBA;
        cfg.output.is_external_memory = 1;
        cfg.output.u.RGBA.rgba = out.rgba.data();
        cfg.output.u.RGBA.stride = tw * 4;
        cfg.output.u.RGBA.size = out.rgba.size();
        const VP8StatusCode st = WebPDecode(data, size, &cfg);
        WebPFreeDecBuffer(&cfg.output);
        if (st != VP8_STATUS_OK) {
            if (err) *err = "webp decode failed (" + std::to_string(st) + ")";
            out = DecodedImage{};
            return false;
        }
        out.hasAlpha = cfg.input.has_alpha && scanAlpha(out.rgba);
        opaqueBounds(out);
        return true;
    }

    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
    if (!px) {
        if (err) *err = std::string("decode failed: ") + stbi_failure_reason();
        return false;
    }
    int tw, th;
    fitWithin(w, h, maxEdge, tw, th);
    out.width = tw;
    out.height = th;
    out.rgba.resize(static_cast<size_t>(tw) * th * 4);
    if (tw == w && th == h) std::memcpy(out.rgba.data(), px, out.rgba.size());
    else downscaleRgba(px, w, h, out.rgba.data(), tw, th);
    stbi_image_free(px);
    flipRows(out.rgba, tw, th);
    out.hasAlpha = (n == 2 || n == 4) && scanAlpha(out.rgba);
    opaqueBounds(out);
    return true;
}

}  // namespace it
