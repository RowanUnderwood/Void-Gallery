#include "modes/TunnelMode.h"

#include <algorithm>

#include "modes/MovingLights.h"
#include "modes/TunnelUv.h"
#include "render/Primitives.h"

namespace it {

int TunnelMode::columnsFor(float radius, float imageSize) const {
    const float circumference = 2.0f * kPi * radius;
    return std::max(3, static_cast<int>(std::floor(circumference / (imageSize * 1.2f))));
}

size_t TunnelMode::texturesNeeded() const {
    const auto& m = ctx_.cfg.cur();
    return static_cast<size_t>(m.tunnelRows) *
           columnsFor(static_cast<float>(m.tunnelRadius), static_cast<float>(m.imageSize));
}

void TunnelMode::enter() {
    tasks_.clear();
    tiles_.clear();
    ctx_.cam.pos = glm::vec3(0.0f);
    ctx_.cam.rot = glm::quat(1, 0, 0, 0);

    const float radius = static_cast<float>(s().tunnelRadius);
    const float size = static_cast<float>(s().imageSize);
    cols_ = columnsFor(radius, size);
    segmentAngle_ = 2.0f * kPi / cols_;
    rowHeight_ = size * 1.2f;
    rows_ = s().tunnelRows;
    totalDepth_ = rows_ * rowHeight_;
    cellWidth_ = segmentAngle_ * radius;

    const float gap = 0.95f;
    const auto geo = prim::tunnelSegment(radius, size, 16, -(segmentAngle_ * gap) / 2, segmentAngle_ * gap);
    segment_.upload(geo.verts, geo.idx);

    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c) tasks_.push_back({c, -(r * rowHeight_)});
}

void TunnelMode::exit() {
    tasks_.clear();
    tiles_.clear();
    segment_.release();
}

void TunnelMode::processSpawns() {
    if (tasks_.empty()) return;
    std::sort(tasks_.begin(), tasks_.end(), [](const Task& a, const Task& b) { return a.z < b.z; });
    while (!tasks_.empty()) {
        TexRef tex = ctx_.textures.take();
        if (!tex) break;
        const Task t = tasks_.back();
        tasks_.pop_back();
        const glm::mat3 uv = tunnelUvTransform(tex->ratio(), t.col, segmentAngle_, cellWidth_,
                                               static_cast<float>(s().imageSize), s().tunnelRotation);
        tiles_.push_back(Tile{std::move(tex), t.col, t.z, uv});
    }
}

void TunnelMode::update(float dt, float time) {
    processSpawns();

    const float ws = static_cast<float>(ctx_.cfg.wobbleSpeed);
    const float wst = static_cast<float>(ctx_.cfg.wobbleStrength);
    float tx = std::sin(time * ws) * wst * 5.0f;
    float ty = std::cos(time * ws * 0.7f) * wst * 3.0f;
    tx += (ctx_.mouse.x * 10.0f - tx) * 0.05f;
    ty += (ctx_.mouse.y * 10.0f - ty) * 0.05f;
    auto& cam = ctx_.cam;
    cam.pos.x += (tx - cam.pos.x) * 0.1f;
    cam.pos.y += (ty - cam.pos.y) * 0.1f;
    cam.rot = quatXYZ(0, 0, std::sin(time * ws * 0.3f) * 0.1f * wst);

    const float move = static_cast<float>(s().cameraSpeed) * dt * 30.0f;
    // Recycle a ring once it is entirely behind the camera (tiles span z +/- imageSize/2). index.html
    // kept tiles until z > 50, wasting about a row of the configured depth behind the camera.
    const float recycleZ = static_cast<float>(s().imageSize) * 0.5f + 5.0f;
    for (size_t i = 0; i < tiles_.size();) {
        Tile& t = tiles_[i];
        t.z += move;
        if (t.z > recycleZ) {
            // Recycle into the far end of the tunnel with a fresh image.
            tasks_.push_back({t.col, t.z - totalDepth_ - move});
            tiles_[i] = std::move(tiles_.back());
            tiles_.pop_back();
        } else {
            ++i;
        }
    }
    // Pending tasks move with the tunnel too (index.html left them at a stale z).
    for (auto& t : tasks_) {
        t.z += move;
        if (t.z > recycleZ) t.z -= totalDepth_;
    }

    ctx_.lights.update(dt, s());
}

void TunnelMode::render() {
    const glm::mat4 view = ctx_.cam.view();
    const float opacity = static_cast<float>(ctx_.cfg.opacity);
    batch_.clear();
    for (const auto& t : tiles_) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, t.z});
        m = glm::rotate(m, t.col * segmentAngle_ + kPi / 2.0f, {0.0f, 0.0f, 1.0f});
        batch_.add(&segment_, m, t.uv, t.tex.get(), view, opacity);
    }
    batch_.draw(ctx_);
    ctx_.lights.drawOrbs(ctx_.gfx, ctx_.cam, static_cast<float>(s().fogDensity));
}

}  // namespace it
