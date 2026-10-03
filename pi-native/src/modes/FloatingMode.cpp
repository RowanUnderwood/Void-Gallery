#include "modes/FloatingMode.h"

#include <algorithm>

#include "modes/MovingLights.h"

namespace it {

size_t FloatingMode::texturesNeeded() const { return static_cast<size_t>(ctx_.cfg.cur().totalImages); }

void FloatingMode::enter() {
    tasks_.clear();
    cards_.clear();
    ctx_.cam.pos = glm::vec3(0.0f);
    ctx_.cam.rot = glm::quat(1, 0, 0, 0);
    for (int i = 0; i < s().totalImages; ++i) queueSpawn(true);
}

void FloatingMode::exit() {
    tasks_.clear();
    cards_.clear();
}

void FloatingMode::queueSpawn(bool randomZ) {
    const float angle = frand() * 2.0f * kPi;
    const float r = (frand() * 0.5f + 0.5f) * static_cast<float>(s().tunnelRadius);
    const float z = randomZ ? -frand() * 1000.0f : -1000.0f;
    glm::vec3 rot(0.0f);
    if (!s().noRotation) rot = {frand() * kPi, frand() * kPi, frand() * kPi};
    tasks_.push_back({{std::cos(angle) * r, std::sin(angle) * r, z}, rot});
}

void FloatingMode::processSpawns() {
    if (tasks_.empty()) return;
    // Nearest (largest z) first, as processSpawnQueue() does.
    std::sort(tasks_.begin(), tasks_.end(), [](const Task& a, const Task& b) { return a.pos.z < b.pos.z; });
    while (!tasks_.empty()) {
        TexRef tex = ctx_.textures.take();  // only taken when a task is waiting
        if (!tex) break;
        const Task t = tasks_.back();
        tasks_.pop_back();
        const float size = static_cast<float>(s().imageSize);
        float w = size, h = size;
        const float ratio = tex->ratio();
        if (ratio > 1.0f) h = w / ratio;
        else w = h * ratio;
        const float rs = static_cast<float>(s().rotationSpeed);
        cards_.push_back(Card{std::move(tex), t.pos, {w, h}, t.rot,
                              {(frand() - 0.5f) * rs, (frand() - 0.5f) * rs, (frand() - 0.5f) * rs},
                              frand() * 0.5f + 1.0f});
    }
}

void FloatingMode::update(float dt, float time) {
    processSpawns();

    const float ws = static_cast<float>(ctx_.cfg.wobbleSpeed);
    const float wst = static_cast<float>(ctx_.cfg.wobbleStrength);
    float tx = std::sin(time * ws) * wst * 5.0f;
    float ty = std::cos(time * ws * 0.7f) * wst * 3.0f;
    tx += (ctx_.mouse.x * 20.0f - tx) * 0.05f;
    ty += (ctx_.mouse.y * 20.0f - ty) * 0.05f;
    auto& cam = ctx_.cam;
    cam.pos.x += (tx - cam.pos.x) * 0.1f;
    cam.pos.y += (ty - cam.pos.y) * 0.1f;
    cam.rot = quatXYZ(0, 0, std::sin(time * ws * 0.3f) * 0.1f * wst);

    const float speed = static_cast<float>(s().cameraSpeed);
    for (size_t i = 0; i < cards_.size();) {
        Card& c = cards_[i];
        c.pos.z += speed * c.velocity * dt * 10.0f;
        if (!s().noRotation) c.rot += c.rotSpeed * dt;
        else c.rot = glm::vec3(0.0f);
        if (c.pos.z > 50.0f) {
            cards_[i] = std::move(cards_.back());  // releases the texture ref
            cards_.pop_back();
            queueSpawn(false);
        } else {
            ++i;
        }
    }
    // Tasks waiting for a texture drift with the cards so they appear where expected.
    for (auto& t : tasks_) t.pos.z = std::min(t.pos.z + speed * dt * 10.0f, 0.0f);

    ctx_.lights.update(dt, s());
}

void FloatingMode::render() {
    const glm::mat4 view = ctx_.cam.view();
    const float opacity = static_cast<float>(ctx_.cfg.opacity);
    batch_.clear();
    for (const auto& c : cards_) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), c.pos);
        if (c.rot != glm::vec3(0.0f)) m *= glm::mat4_cast(quatXYZ(c.rot.x, c.rot.y, c.rot.z));
        m = glm::scale(m, glm::vec3(c.size, 1.0f));
        batch_.add(&ctx_.gfx.quad, m, glm::mat3(1.0f), c.tex.get(), view, opacity);
    }
    batch_.draw(ctx_);
    ctx_.lights.drawOrbs(ctx_.gfx, ctx_.cam, static_cast<float>(s().fogDensity));
}

}  // namespace it
