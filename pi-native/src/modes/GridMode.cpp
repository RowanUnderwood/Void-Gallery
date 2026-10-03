#include "modes/GridMode.h"

#include <algorithm>
#include <cstdio>

#include "modes/MovingLights.h"

namespace it {

namespace {
constexpr int kLayers = 5;
}

size_t GridMode::texturesNeeded() const {
    const auto& m = ctx_.cfg.cur();
    return static_cast<size_t>(m.gridCols * m.gridRows * 2);
}

glm::vec2 GridMode::targetPosition(int c, int r) const {
    const float size = static_cast<float>(ctx_.cfg.cur().imageSize);
    const float offX = (ctx_.cfg.cur().gridCols * size) / 2 - size / 2;
    const float offY = (ctx_.cfg.cur().gridRows * size) / 2 - size / 2;
    return {c * size - offX, r * size - offY};
}

void GridMode::enter() {
    layers_.assign(kLayers, Layer{});
    tasks_.clear();
    ctx_.cam.rot = glm::quat(1, 0, 0, 0);
    targetC_ = s().gridCols / 2;
    targetR_ = s().gridRows / 2;
    target_ = targetPosition(targetC_, targetR_);
    ctx_.cam.pos = glm::vec3(target_, 0.0f);
    const float spacing = static_cast<float>(s().gridSpacing);
    for (int i = 0; i < kLayers; ++i) {
        layers_[i].z = i * -spacing - 400.0f;
        createTasks(i);
    }
}

void GridMode::exit() {
    layers_.clear();
    tasks_.clear();
}

void GridMode::createTasks(int layer) {
    for (int r = 0; r < s().gridRows; ++r)
        for (int c = 0; c < s().gridCols; ++c) tasks_.push_back({layer, targetPosition(c, r)});
}

void GridMode::pickNextTarget() {
    const int cols = s().gridCols, rows = s().gridRows, margin = s().gridEdgeBuffer;
    const int maxDevC = std::max(1, static_cast<int>(std::lround(cols * s().pathRandomness)));
    const int maxDevR = std::max(1, static_cast<int>(std::lround(rows * s().pathRandomness)));
    int c = targetC_ + static_cast<int>(frand() * (maxDevC * 2 + 1)) - maxDevC;
    int r = targetR_ + static_cast<int>(frand() * (maxDevR * 2 + 1)) - maxDevR;
    c = cols > margin * 2 ? std::clamp(c, margin, cols - 1 - margin) : std::clamp(c, 0, cols - 1);
    r = rows > margin * 2 ? std::clamp(r, margin, rows - 1 - margin) : std::clamp(r, 0, rows - 1);
    targetC_ = c;
    targetR_ = r;
    target_ = targetPosition(c, r);
}

void GridMode::recycle(int layer) {
    float minZ = 0.0f;
    for (const auto& l : layers_) minZ = std::min(minZ, l.z);
    layers_[layer].z = minZ - static_cast<float>(s().gridSpacing);
    layers_[layer].tiles.clear();
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [layer](const Task& t) { return t.layer == layer; }),
                 tasks_.end());
    createTasks(layer);
    pickNextTarget();
}

void GridMode::processSpawns() {
    if (tasks_.empty()) return;
    std::sort(tasks_.begin(), tasks_.end(),
              [this](const Task& a, const Task& b) { return layers_[a.layer].z < layers_[b.layer].z; });
    const float size = static_cast<float>(s().imageSize);
    while (!tasks_.empty()) {
        TexRef tex = ctx_.textures.take();
        if (!tex) break;
        const Task t = tasks_.back();
        tasks_.pop_back();
        float w = size, h = size;
        const float ratio = tex->ratio();
        if (ratio > 1.0f) h = w / ratio;
        else w = h * ratio;
        layers_[t.layer].tiles.push_back({std::move(tex), t.xy, {w, h}});
    }
}

void GridMode::update(float dt, float) {
    processSpawns();
    const float speed = static_cast<float>(s().cameraSpeed);
    const float move = speed * dt * 100.0f;
    auto& cam = ctx_.cam;
    const glm::vec2 look = target_ + ctx_.mouse * 50.0f;
    cam.pos.x += (look.x - cam.pos.x) * dt * (speed * 0.5f);
    cam.pos.y += (look.y - cam.pos.y) * dt * (speed * 0.5f);
    for (int i = 0; i < static_cast<int>(layers_.size()); ++i) {
        layers_[i].z += move;
        if (layers_[i].z > 150.0f) recycle(i);
    }
    ctx_.lights.update(dt, s());
}

std::string GridMode::status() const {
    size_t tiles = 0;
    for (const auto& l : layers_) tiles += l.tiles.size();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "grid tiles %zu  pending %zu  camera %.0f, %.0f -> target %.0f, %.0f", tiles,
                  tasks_.size(), ctx_.cam.pos.x, ctx_.cam.pos.y, target_.x, target_.y);
    return buf;
}

void GridMode::render() {
    const glm::mat4 view = ctx_.cam.view();
    const float opacity = static_cast<float>(ctx_.cfg.opacity);
    // Frustum-ish cull: skip tiles far outside the view cone (cheap, saves draw calls at 12x12).
    const float tanHalf = std::tan(glm::radians(ctx_.cam.fovDeg * 0.5f));
    batch_.clear();
    for (const auto& l : layers_) {
        const float depth = ctx_.cam.pos.z - l.z;
        if (depth <= 0.0f) continue;
        const float halfH = depth * tanHalf + s().imageSize;
        const float halfW = halfH * ctx_.cam.aspect + s().imageSize;
        for (const auto& t : l.tiles) {
            if (std::abs(t.xy.x - ctx_.cam.pos.x) > halfW || std::abs(t.xy.y - ctx_.cam.pos.y) > halfH) continue;
            glm::mat4 m = glm::translate(glm::mat4(1.0f), {t.xy, l.z});
            m = glm::scale(m, glm::vec3(t.size, 1.0f));
            batch_.add(&ctx_.gfx.quad, m, glm::mat3(1.0f), t.tex.get(), view, opacity);
        }
    }
    batch_.draw(ctx_);
    ctx_.lights.drawOrbs(ctx_.gfx, ctx_.cam, static_cast<float>(s().fogDensity));
}

}  // namespace it
