#include "modes/FloatingMode.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>

#include "modes/MovingLights.h"

namespace it {

size_t FloatingMode::texturesNeeded() const { return static_cast<size_t>(ctx_.cfg.cur().totalImages); }

std::string FloatingMode::collisions() const { return ctx_.cfg.cur().floatingCollisions; }

std::string FloatingMode::status() const {
    char buf[160];
    const std::string contacts = lastContacts_ < 0 ? "n/a" : std::to_string(lastContacts_);
    std::snprintf(buf, sizeof(buf), "floating %zu cards  collisions %s%s  contacts %s  solver %.2f ms", cards_.size(),
                  activeCollisions_.c_str(), activeCollisions_ == "physics" ? (physicsSpin_ ? " (spin)" : " (no spin)") : "",
                  contacts.c_str(), solverMs_);
    return buf;
}

void FloatingMode::enter() {
    tasks_.clear();
    cards_.clear();
    physics_.reset();
    activeCollisions_ = collisions();
    physicsSpin_ = !s().noRotation;
    if (activeCollisions_ == "physics") physics_ = std::make_unique<FloatingPhysics>(physicsSpin_);
    ctx_.cam.pos = glm::vec3(0.0f);
    ctx_.cam.rot = glm::quat(1, 0, 0, 0);
    for (int i = 0; i < s().totalImages; ++i) queueSpawn(true);
}

void FloatingMode::exit() {
    tasks_.clear();
    cards_.clear();
    physics_.reset();
}

// With collisions on, avoid spawning on top of another card (physics would fling them apart and
// make-way would have to untangle them on screen).
bool FloatingMode::spawnSpotFree(const glm::vec3& p) const {
    const float size = static_cast<float>(ctx_.cfg.cur().imageSize);
    auto clash = [&](const glm::vec3& q) {
        return std::abs(q.z - p.z) < size * 0.5f && std::abs(q.x - p.x) < size && std::abs(q.y - p.y) < size;
    };
    for (const auto& c : cards_)
        if (clash(c.body.pos)) return false;
    for (const auto& t : tasks_)
        if (clash(t.pos)) return false;
    return true;
}

void FloatingMode::queueSpawn(bool randomZ) {
    const bool avoid = collisions() != "off";
    glm::vec3 pos(0.0f);
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float angle = frand() * 2.0f * kPi;
        const float r = (frand() * 0.5f + 0.5f) * static_cast<float>(s().tunnelRadius);
        const float z = randomZ ? -frand() * 1000.0f : -1000.0f - frand() * (avoid ? 150.0f : 0.0f);
        pos = {std::cos(angle) * r, std::sin(angle) * r, z};
        if (!avoid || spawnSpotFree(pos)) break;
    }
    glm::vec3 rot(0.0f);
    if (!s().noRotation) rot = {frand() * kPi, frand() * kPi, frand() * kPi};
    tasks_.push_back({pos, rot});
}

void FloatingMode::processSpawns() {
    if (tasks_.empty()) return;
    // Nearest (largest z) first, as processSpawnQueue() does.
    std::sort(tasks_.begin(), tasks_.end(), [](const Task& a, const Task& b) { return a.pos.z < b.pos.z; });
    const float speed = static_cast<float>(s().cameraSpeed);
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
        Card c;
        c.size = {w, h};
        c.rot = t.rot;
        c.rotSpeed = {(frand() - 0.5f) * rs, (frand() - 0.5f) * rs, (frand() - 0.5f) * rs};
        c.velocity = frand() * 0.5f + 1.0f;
        c.orient = quatXYZ(t.rot.x, t.rot.y, t.rot.z);
        // Collision footprint = the image's visible pixels (tight bounds for cut-out images).
        const float* o = tex->opaque;
        c.body.pos = t.pos;
        c.body.half = {0.5f * (o[2] - o[0]) * w, 0.5f * (o[3] - o[1]) * h};
        c.body.offset = {((o[0] + o[2]) * 0.5f - 0.5f) * w, ((o[1] + o[3]) * 0.5f - 0.5f) * h};
        c.body.cruise = speed * c.velocity * 10.0f;
        c.body.vel = {0.0f, 0.0f, c.body.cruise};
        c.tex = std::move(tex);
        if (physics_) c.physId = physics_->add(c.body.pos, c.orient, c.body.half, c.body.offset, c.body.vel, c.rotSpeed);
        cards_.push_back(std::move(c));
    }
}

void FloatingMode::removeCard(size_t i) {
    if (physics_) physics_->remove(cards_[i].physId);
    cards_[i] = std::move(cards_.back());  // releases the texture ref
    cards_.pop_back();
}

void FloatingMode::update(float dt, float time) {
    // The collision setting or "Stop Rotation" changed: rebuild so every card uses the new model.
    if (collisions() != activeCollisions_ || (activeCollisions_ == "physics" && physicsSpin_ == s().noRotation)) {
        exit();
        enter();
    }
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
    for (auto& c : cards_) c.body.cruise = speed * c.velocity * 10.0f;  // the Speed slider is live

    const uint64_t t0 = SDL_GetPerformanceCounter();
    lastContacts_ = 0;
    if (activeCollisions_ == "physics" && physics_) {
        FloatingPhysics::Params p;
        p.radius = static_cast<float>(s().tunnelRadius);
        for (const auto& c : cards_) physics_->setCruise(c.physId, c.body.cruise);
        physics_->step(dt, p);
        for (auto& c : cards_) physics_->read(c.physId, c.body.pos, c.orient);
        lastContacts_ = -1;  // not tracked in physics mode
    } else if (activeCollisions_ == "makeway") {
        MakeWayParams p;
        p.radius = static_cast<float>(s().tunnelRadius);
        scratch_.resize(cards_.size());
        for (size_t i = 0; i < cards_.size(); ++i) scratch_[i] = cards_[i].body;
        // Sub-step so closing speeds can never jump the minimum gap in one step (low frame rates).
        const int steps = std::clamp(static_cast<int>(std::ceil(dt / (1.0f / 120.0f))), 1, 8);
        for (int k = 0; k < steps; ++k) lastContacts_ += stepMakeWay(scratch_, dt / steps, p);
        for (size_t i = 0; i < cards_.size(); ++i) cards_[i].body = scratch_[i];
    } else {
        for (auto& c : cards_) c.body.pos.z += c.body.cruise * dt;  // index.html: no interaction
    }
    solverMs_ = solverMs_ * 0.9f +
                0.1f * static_cast<float>((SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency());

    for (size_t i = 0; i < cards_.size();) {
        Card& c = cards_[i];
        if (activeCollisions_ != "physics") {
            if (!s().noRotation) c.rot += c.rotSpeed * dt;
            else c.rot = glm::vec3(0.0f);
        }
        if (c.body.pos.z > 50.0f) {
            removeCard(i);
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
    const bool physics = activeCollisions_ == "physics";
    batch_.clear();
    for (const auto& c : cards_) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), c.body.pos);
        if (physics) m *= glm::mat4_cast(c.orient);
        else if (c.rot != glm::vec3(0.0f)) m *= glm::mat4_cast(quatXYZ(c.rot.x, c.rot.y, c.rot.z));
        m = glm::scale(m, glm::vec3(c.size, 1.0f));
        batch_.add(&ctx_.gfx.quad, m, glm::mat3(1.0f), c.tex.get(), view, opacity);
    }
    batch_.draw(ctx_);
    ctx_.lights.drawOrbs(ctx_.gfx, ctx_.cam, static_cast<float>(s().fogDensity));
}

}  // namespace it
