#include "modes/MazeMode.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "render/Primitives.h"

namespace it {

namespace {

constexpr float kWallHeight = kMazeCell * 1.5f;
constexpr float kEyeHeight = kMazeCell * 0.6f;
constexpr float kPaintingY = kMazeCell * 1.5f * 0.5f;
constexpr float kSpotRange = 70.0f;
constexpr float kSpotPenumbra = 0.4f;
constexpr float kLightRadius = 120.0f;   // paintings further than this never get a spotlight
constexpr float kExitRadius = 200.0f;
constexpr float kExitIntensity = 2000.0f;
constexpr float kExitRange = 150.0f;
constexpr float kFadeTime = 0.4f;
constexpr float kSpotFadeTime = 0.2f;

void dirDelta(char d, int& dr, int& dc) {
    switch (d) {
        case 'N': dr = -1; dc = 0; break;
        case 'S': dr = 1; dc = 0; break;
        case 'W': dr = 0; dc = -1; break;
        default: dr = 0; dc = 1; break;  // 'E'
    }
}
float faceRotY(char d) {  // FACE_ROT_Y
    switch (d) {
        case 'N': return 0.0f;
        case 'S': return kPi;
        case 'W': return kPi / 2;
        default: return -kPi / 2;
    }
}
float paintingRotY(char d) {  // PAINTING_ROT_Y
    switch (d) {
        case 'N': return kPi;
        case 'S': return 0.0f;
        case 'W': return -kPi / 2;
        default: return kPi / 2;
    }
}
struct Turns { char straight, right, left, back; };
Turns turnMap(char facing) {  // TURN_MAP
    switch (facing) {
        case 'N': return {'N', 'E', 'W', 'S'};
        case 'S': return {'S', 'W', 'E', 'N'};
        case 'E': return {'E', 'S', 'N', 'W'};
        default: return {'W', 'N', 'S', 'E'};
    }
}
glm::vec3 cellToWorld(int row, int col) { return {col * kMazeCell, kEyeHeight, row * kMazeCell}; }

// Basis whose +Z points from `from` to `to` (Object3D.lookAt for non-camera objects).
glm::mat4 lookAtBasis(const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 z = glm::normalize(to - from);
    glm::vec3 up(0, 1, 0);
    if (std::abs(glm::dot(z, up)) > 0.999f) up = glm::vec3(0, 0, 1);
    const glm::vec3 x = glm::normalize(glm::cross(up, z));
    const glm::vec3 y = glm::cross(z, x);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0);
    m[1] = glm::vec4(y, 0);
    m[2] = glm::vec4(z, 0);
    m[3] = glm::vec4(from, 1);
    return m;
}

}  // namespace

MazeMode::MazeMode(ModeContext& ctx) : Mode(ctx), rng_(std::random_device{}()) {}

MazeMode::~MazeMode() {
    for (auto& m : mats_) {
        if (m.color) glDeleteTextures(1, &m.color);
        if (m.normal) glDeleteTextures(1, &m.normal);
    }
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
}

size_t MazeMode::texturesNeeded() const { return static_cast<size_t>(ctx_.cfg.cur().mazeImageCount) + 5; }

bool MazeMode::busy() const {
    if (!built_) return true;
    for (const auto& m : mats_)
        if (m.pending > 0 && !m.color) return true;  // first load only; swaps happen in place
    return false;
}

std::string MazeMode::status() const {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "maze %dx%d  paintings %zu/%zu  exit (%d,%d)%s", grid_.size(), grid_.size(),
                  paintings_.size(), paintings_.size() + tasks_.size(), exitRow_, exitCol_,
                  exitVisible_ ? " visible" : "");
    return buf;
}

// ----------------------------------------------------------------------------- materials
void MazeMode::requestMaterial(int surface, const std::string& name) {
    Material& m = mats_[surface];
    m.name = name;
    m.token = ++matToken_;
    m.pending = 2;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const std::string ext = (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, "-jpg") == 0) ? "jpg" : "png";
    const std::string base = "textures/" + name + "/" + name;
    const uint64_t token = m.token;
    const int aniso = std::min(ctx_.cfg.cur().anisotropyLevel, caps::kMaxAnisotropy);

    auto finish = [this, surface, token, aniso](bool isNormal, bool ok, DecodedImage&& img, const std::string& err) {
        Material& mat = mats_[surface];
        if (mat.token != token) return;  // superseded by a newer request
        --mat.pending;
        if (!ok) {
            std::fprintf(stderr, "[maze] texture load failed: %s\n", err.c_str());
            return;
        }
        GLuint& slot = isNormal ? mat.normal : mat.color;
        if (slot) glDeleteTextures(1, &slot);
        slot = gl::createTexture(img, gl::TextureOptions{!isNormal, true, true, aniso});
    };
    ctx_.loader.load(ctx_.source, base + "_Color." + ext, 1024,
                     [finish](bool ok, DecodedImage&& img, const std::string& err) mutable {
                         finish(false, ok, std::move(img), err);
                     });
    ctx_.loader.load(ctx_.source, base + "_NormalGL." + ext, 1024,
                     [finish](bool ok, DecodedImage&& img, const std::string& err) mutable {
                         finish(true, ok, std::move(img), err);
                     });
}

void MazeMode::syncMaterials() {
    const ModeSettings& m = ctx_.cfg.cur();
    const std::string* names[kSurfaceCount] = {&m.mazeWallTexture, &m.mazeFloorTexture, &m.mazeCeilingTexture};
    for (int i = 0; i < kSurfaceCount; ++i)
        if (mats_[i].name != *names[i]) requestMaterial(i, *names[i]);
}

void MazeMode::reloadMaterials() {
    for (auto& m : mats_) m.name.clear();
    syncMaterials();
}

void MazeMode::setAnisotropy(int level) {
    for (auto& m : mats_) {
        if (m.color) gl::setAnisotropy(m.color, level);
        if (m.normal) gl::setAnisotropy(m.normal, level);
    }
}

void MazeMode::resetSpotColors() {
    const bool random = ctx_.cfg.cur().lightColorMode == "random";
    for (auto& sp : spots_) sp.srgb = random ? hslToRgb(frand(), 1.0f, 0.5f) : hexColor(0xfff5e0);
}

// ----------------------------------------------------------------------------- build
void MazeMode::enter() {
    syncMaterials();
    resetSpotColors();
    buildMaze();
    regen_ = Regen::None;
    fade_ = 0.0f;
}

void MazeMode::exit() {
    clearMaze();
    built_ = false;
}

void MazeMode::clearMaze() {
    walls_.release();
    floor_.release();
    ceiling_.release();
    frames_.release();
    mapWalls_.release();
    paintings_.clear();
    tasks_.clear();
    for (auto& sp : spots_) {
        sp.painting = -1;
        sp.level = 0.0f;
    }
    exitVisible_ = false;
}

void MazeMode::buildMaze() {
    clearMaze();
    const ModeSettings& m = ctx_.cfg.cur();
    grid_.generate(std::clamp(m.mazeComplexity, 5, caps::kMaxMazeComplexity), rng_);
    const int n = grid_.size();

    // Walls: only faces bordering an open cell (index.html drew full boxes for every wall cell).
    const auto faces = grid_.wallFaces();
    {
        prim::Geometry g;
        g.verts.reserve(faces.size() * 4);
        g.idx.reserve(faces.size() * 6);
        for (const auto& f : faces) {
            const glm::vec3 nrm(static_cast<float>(f.dc), 0.0f, static_cast<float>(f.dr));
            const glm::vec3 t(static_cast<float>(f.dr), 0.0f, static_cast<float>(-f.dc));  // cross(up, n)
            const glm::vec3 c(f.col * kMazeCell + f.dc * kMazeCell * 0.5f, 0.0f,
                              f.row * kMazeCell + f.dr * kMazeCell * 0.5f);
            const glm::vec3 lo(0, -4.0f, 0), hi(0, kWallHeight, 0);
            const float hw = kMazeCell * 0.5f;
            prim::appendQuad(g, c - t * hw + lo, c + t * hw + lo, c + t * hw + hi, c - t * hw + hi, nrm,
                             glm::vec4(t, 1.0f));
        }
        walls_.upload(g.verts, g.idx);
    }
    // Floor / ceiling sized to the maze (UVs come from world XZ in the shader).
    {
        const float x0 = -kMazeCell, x1 = n * kMazeCell;
        prim::Geometry f, c;
        prim::appendQuad(f, {x0, 0, x1}, {x1, 0, x1}, {x1, 0, x0}, {x0, 0, x0}, {0, 1, 0}, {1, 0, 0, -1});
        prim::appendQuad(c, {x0, kWallHeight, x0}, {x1, kWallHeight, x0}, {x1, kWallHeight, x1}, {x0, kWallHeight, x1},
                         {0, -1, 0}, {1, 0, 0, 1});
        floor_.upload(f.verts, f.idx);
        ceiling_.upload(c.verts, c.idx);
    }
    // Minimap wall quads in unit map space.
    {
        prim::Geometry g;
        const float cs = 1.0f / n;
        for (int r = 0; r < n; ++r)
            for (int col = 0; col < n; ++col)
                if (grid_.wall(r, col))
                    prim::appendQuad(g, {col * cs, r * cs, 0}, {(col + 1) * cs, r * cs, 0},
                                     {(col + 1) * cs, (r + 1) * cs, 0}, {col * cs, (r + 1) * cs, 0}, {0, 0, 1},
                                     {1, 0, 0, 1});
        mapWalls_.upload(g.verts, g.idx);
    }
    if (arrow_.empty()) {
        prim::Geometry g;
        g.verts = {{{0, -0.8f, 0}}, {{-0.5f, 0.6f, 0}}, {{0, 0.3f, 0}}, {{0.5f, 0.6f, 0}}};
        g.idx = {0, 1, 2, 0, 2, 3};
        arrow_.upload(g.verts, g.idx);
    }

    // Paintings are queued and spawn nearest-first as textures arrive.
    std::vector<WallFace> shuffled = faces;
    std::shuffle(shuffled.begin(), shuffled.end(), rng_);
    shuffled.resize(std::min<size_t>(shuffled.size(), static_cast<size_t>(std::max(0, m.mazeImageCount))));
    for (const auto& f : shuffled) {
        tasks_.push_back({f, {f.col * kMazeCell + f.dc * kMazeCell * 0.5f, kPaintingY,
                              f.row * kMazeCell + f.dr * kMazeCell * 0.5f}});
    }
    framesDirty_ = true;

    int startIdx = 1;
    grid_.startAndExit(startIdx, exitRow_, exitCol_);
    const glm::vec3 start = cellToWorld(startIdx, startIdx);
    ctx_.cam.pos = start;
    ctx_.cam.rot = quatYXZ(0, kPi, 0);  // face south
    nav_ = Nav{};
    nav_.row = nav_.trow = startIdx;
    nav_.col = nav_.tcol = startIdx;
    nav_.facing = 'S';
    nav_.startPos = nav_.endPos = start;
    nav_.phase = Nav::Moving;
    nav_.progress = 1.0f;  // triggers advanceNav() on the first update
    built_ = true;
}

void MazeMode::processSpawns() {
    if (tasks_.empty()) return;
    const glm::vec3 cam = ctx_.cam.pos;
    // Farthest first so the nearest is at the back (the web version sorted by row instead).
    std::sort(tasks_.begin(), tasks_.end(), [&](const Task& a, const Task& b) {
        const glm::vec3 da = a.worldPos - cam, db = b.worldPos - cam;
        return glm::dot(da, da) > glm::dot(db, db);
    });
    while (!tasks_.empty()) {
        TexRef tex = ctx_.textures.take();
        if (!tex) break;
        const Task t = tasks_.back();
        tasks_.pop_back();
        float pW = kMazeCell * 0.58f;
        float pH = pW / tex->ratio();
        const float maxH = kMazeCell * 1.2f;  // keep very tall images inside the wall
        if (pH > maxH) {
            pW *= maxH / pH;
            pH = maxH;
        }
        Painting p;
        p.tex = std::move(tex);
        p.pos = t.worldPos;
        p.normal = glm::vec3(static_cast<float>(t.face.dc), 0.0f, static_cast<float>(t.face.dr));
        p.rotY = paintingRotY(t.face.face);
        p.w = pW;
        p.h = pH;
        paintings_.push_back(std::move(p));
        framesDirty_ = true;
    }
}

void MazeMode::rebuildFrames() {
    // All frames in one mesh: one draw call instead of four meshes per painting.
    prim::Geometry g;
    for (const auto& p : paintings_) {
        const glm::mat4 xf = glm::rotate(glm::translate(glm::mat4(1.0f), p.pos), p.rotY, {0, 1, 0});
        g.append(prim::box({p.w + 1.1f, p.h + 1.1f, 0.8f}, true), xf);
    }
    frames_.upload(g.verts, g.idx, GL_TRIANGLES, true);
    framesDirty_ = false;
}

// ----------------------------------------------------------------------------- navigation
void MazeMode::startRegen() {
    if (regen_ != Regen::None) return;
    if (ctx_.cfg.globals.pi.regenTransition == "cut") {
        buildMaze();
        return;
    }
    regen_ = Regen::FadingOut;
}

void MazeMode::advanceNav() {
    nav_.row = nav_.trow;
    nav_.col = nav_.tcol;
    if (nav_.row == exitRow_ && nav_.col == exitCol_) {
        startRegen();
        return;
    }
    const Turns t = turnMap(nav_.facing);
    char options[4] = {t.straight, t.right, t.left, t.back};
    std::shuffle(options, options + 3, rng_);  // back stays last: only used at dead ends
    char chosen = 0;
    for (char d : options) {
        int dr, dc;
        dirDelta(d, dr, dc);
        if (grid_.canMove(nav_.row, nav_.col, dr, dc)) {
            chosen = d;
            break;
        }
    }
    if (!chosen) {
        startRegen();
        return;
    }
    if (chosen != nav_.facing) {
        nav_.startQ = ctx_.cam.rot;
        nav_.endQ = quatYXZ(0, faceRotY(chosen), 0);
        nav_.facing = chosen;
        nav_.phase = Nav::Turning;
        nav_.progress = 0.0f;
    } else {
        startMoving();
    }
}

void MazeMode::startMoving() {
    int dr, dc;
    dirDelta(nav_.facing, dr, dc);
    nav_.trow = nav_.row + dr * 2;
    nav_.tcol = nav_.col + dc * 2;
    nav_.startPos = ctx_.cam.pos;
    nav_.startPos.y = kEyeHeight;
    nav_.endPos = cellToWorld(nav_.trow, nav_.tcol);
    nav_.phase = Nav::Moving;
    nav_.progress = 0.0f;
    nav_.headBob = 0.0f;
}

void MazeMode::updateNav(float dt) {
    const ModeSettings& m = ctx_.cfg.cur();
    const float walk = static_cast<float>(m.mazeWalkingSpeed);
    const bool modern = m.navStyle == "modern";
    const float stepDur = 1.2f / std::max(0.1f, walk);
    const float turnDur = modern ? stepDur * 0.8f : 0.25f;
    auto& cam = ctx_.cam;

    if (nav_.phase == Nav::Turning) {
        nav_.progress = std::min(nav_.progress + dt / turnDur, 1.0f);
        cam.rot = glm::slerp(nav_.startQ, nav_.endQ, smoothstep01(nav_.progress));
        if (modern && nav_.progress > 0.3f) cam.translateZ(-walk * dt * 8.0f);
        if (nav_.progress >= 1.0f) startMoving();
    }
    if (nav_.phase == Nav::Moving) {
        nav_.progress = std::min(nav_.progress + dt / stepDur, 1.0f);
        nav_.headBob += dt * kPi * 2.0f;
        cam.pos = glm::mix(nav_.startPos, nav_.endPos, smoothstep01(nav_.progress));
        if (nav_.progress >= 1.0f) advanceNav();
    }
    cam.pos.y = kEyeHeight + (m.navStyle == "win95" ? std::sin(nav_.headBob) * 0.35f : 0.0f);

    if (modern && nav_.phase == Nav::Moving) {  // absolute mouse-look offset from the base facing
        const glm::quat base = quatYXZ(0, faceRotY(nav_.facing), 0);
        cam.rot = base * axisAngle({0, 1, 0}, -ctx_.mouse.x * 0.4f) * axisAngle({1, 0, 0}, ctx_.mouse.y * 0.2f);
    }
}

// ----------------------------------------------------------------------------- lighting
glm::vec3 MazeMode::spotPosition(const Painting& p) const {
    return p.pos + p.normal * 2.0f + glm::vec3(0, static_cast<float>(ctx_.cfg.cur().mazeSpotlightHeight), 0);
}

glm::vec3 MazeMode::exitPosition() const { return cellToWorld(exitRow_, exitCol_); }

void MazeMode::updateSpots(float dt) {
    const glm::vec3 cam = ctx_.cam.pos;
    struct Cand { float d; int i; };
    std::vector<Cand> cands;
    cands.reserve(32);
    for (int i = 0; i < static_cast<int>(paintings_.size()); ++i) {
        const Painting& p = paintings_[i];
        const float d = glm::distance(cam, p.pos);
        if (d > kLightRadius) continue;
        const glm::vec3 probe = p.pos + p.normal * 0.5f;  // just inside the open cell in front of it
        if (!grid_.lineOfSight(cam.x, cam.z, probe.x, probe.z)) continue;
        cands.push_back({d, i});
    }
    const size_t keep = std::min(cands.size(), spots_.size());
    std::partial_sort(cands.begin(), cands.begin() + keep, cands.end(),
                      [](const Cand& a, const Cand& b) { return a.d < b.d; });
    auto wanted = [&](int painting) {
        for (size_t k = 0; k < keep; ++k)
            if (cands[k].i == painting) return true;
        return false;
    };
    for (auto& sp : spots_)
        if (sp.painting >= 0 && !wanted(sp.painting)) {
            sp.painting = -1;
            sp.level = 0.0f;
        }
    for (size_t k = 0; k < keep; ++k) {
        const int pi = cands[k].i;
        bool assigned = false;
        for (const auto& sp : spots_) assigned |= sp.painting == pi;
        if (assigned) continue;
        for (auto& sp : spots_)
            if (sp.painting < 0) {
                sp.painting = pi;
                sp.level = 0.0f;
                break;
            }
    }
    for (auto& sp : spots_)
        if (sp.painting >= 0) sp.level = std::min(1.0f, sp.level + dt / kSpotFadeTime);

    glm::vec3 ep = exitPosition();
    const float de = glm::distance(cam, ep);
    exitVisible_ = de <= kExitRadius && grid_.lineOfSight(cam.x, cam.z, ep.x, ep.z);
}

void MazeMode::ensureShadowTarget(int size) {
    if (shadowTex_ && shadowSize_ == size) return;
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    shadowSize_ = size;
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT16, size, size);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
}

void MazeMode::renderShadow(const glm::vec3& lightPos, const glm::vec3& target, const glm::vec3& up) {
    GLint prevFbo = 0, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, vp);

    const int size = std::min(ctx_.cfg.cur().mazeShadowRes, caps::kMaxShadowRes) <= 256 ? 256 : 512;
    ensureShadowTarget(size);
    const float angle = static_cast<float>(ctx_.cfg.cur().mazeSpotlightAngle);
    const glm::mat4 proj = glm::perspective(std::min(2.0f * angle + 0.1f, kPi * 0.95f), 1.0f, 1.0f, kSpotRange);
    const glm::mat4 view = glm::lookAt(lightPos, target, up);
    const glm::mat4 vp4 = proj * view;
    const glm::mat4 bias = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    shadowMat_ = bias * vp4;

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, size, size);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glDisable(GL_CULL_FACE);
    auto& sh = ctx_.gfx.depth;
    sh.use();
    sh.set("uMVP", vp4);
    walls_.draw();
    frames_.draw();
    ctx_.gfx.drawCalls += 2;
    glDisable(GL_POLYGON_OFFSET_FILL);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

// ----------------------------------------------------------------------------- frame
void MazeMode::update(float dt, float time) {
    time_ = time;
    if (!built_) return;
    processSpawns();
    if (framesDirty_) rebuildFrames();

    if (regen_ == Regen::FadingOut) {
        fade_ += dt / kFadeTime;
        if (fade_ >= 1.0f) {
            fade_ = 1.0f;
            buildMaze();  // generation + mesh build take a few ms, hidden behind the black frame
            regen_ = Regen::FadingIn;
        }
    } else if (regen_ == Regen::FadingIn) {
        fade_ -= dt / kFadeTime;
        if (fade_ <= 0.0f) {
            fade_ = 0.0f;
            regen_ = Regen::None;
        }
    }
    if (regen_ == Regen::None && nav_.phase != Nav::Idle) updateNav(dt);

    updateSpots(dt);
    exitSpinY_ += dt;
    exitSpinZ_ += dt * 0.5f;
}

void MazeMode::render() {
    if (!built_) return;
    const ModeSettings& m = ctx_.cfg.cur();
    const auto& pi = ctx_.cfg.globals.pi;
    const Camera& cam = ctx_.cam;
    const float fog = static_cast<float>(m.fogDensity);
    const float intensity = static_cast<float>(m.lightIntensity);
    const float angle = static_cast<float>(m.mazeSpotlightAngle);

    // Gather active spotlights.
    glm::vec3 sPos[4], sDir[4], sCol[4];
    float sCos[4], sPen[4], sRange[4];
    int nSpots = 0;
    shadowSpot_ = -1;
    float nearest = 1e9f;
    const int maxSpots = std::min<int>(spots_.size(), std::max(1, ctx_.maxLightsPerDraw));
    for (const auto& sp : spots_) {
        if (sp.painting < 0 || nSpots >= maxSpots) continue;
        const Painting& p = paintings_[sp.painting];
        sPos[nSpots] = spotPosition(p);
        sDir[nSpots] = glm::normalize(p.pos - sPos[nSpots]);
        sCol[nSpots] = srgbToLinear(sp.srgb) * intensity * sp.level;
        sCos[nSpots] = std::cos(angle);
        sPen[nSpots] = std::cos(angle * (1.0f - kSpotPenumbra));
        sRange[nSpots] = kSpotRange;
        const float d = glm::distance(cam.pos, p.pos);
        if (pi.mazeShadows == "nearest" && d < nearest) {
            nearest = d;
            shadowSpot_ = nSpots;
        }
        ++nSpots;
    }
    if (shadowSpot_ >= 0) {
        // Spot direction always has a horizontal component (light sits 2 units off the wall), so
        // its XZ part is a safe "up" vector for the light's view matrix.
        const glm::vec3 d = sDir[shadowSpot_];
        renderShadow(sPos[shadowSpot_], sPos[shadowSpot_] + d, glm::normalize(glm::vec3(d.x, 0.0f, d.z)));
    }

    // Depth pre-pass over the opaque maze geometry, so the lighting below runs once per pixel
    // instead of for every wall layer behind the nearest one.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    {
        auto& dp = ctx_.gfx.mazeDepth;
        dp.use();
        dp.set("uModel", glm::mat4(1.0f));
        dp.set("uView", cam.view());
        dp.set("uProj", cam.proj());
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        walls_.draw();
        floor_.draw();
        ceiling_.draw();
        frames_.draw();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        ctx_.gfx.drawCalls += 4;
    }
    glDepthMask(GL_FALSE);  // depth for these surfaces is final

    auto& sh = ctx_.gfx.maze;
    sh.use();
    sh.set("uView", cam.view());
    sh.set("uProj", cam.proj());
    sh.set("uCamPos", cam.pos);
    sh.set("uAmbient", glm::vec3(static_cast<float>(m.ambientIntensity)));
    sh.set("uFogDensity", fog);
    sh.set("uOpacity", 1.0f);
    sh.set("uNumSpots", nSpots);
    if (nSpots > 0) {
        sh.setArray("uSpotPos", sPos, nSpots);
        sh.setArray("uSpotDir", sDir, nSpots);
        sh.setArray("uSpotColor", sCol, nSpots);
        sh.setArray("uSpotCos", sCos, nSpots);
        sh.setArray("uSpotPenCos", sPen, nSpots);
        sh.setArray("uSpotRange", sRange, nSpots);
    }
    sh.set("uShadowIdx", shadowSpot_);
    sh.set("uShadowMat", shadowMat_);
    sh.set("uHasPoint", exitVisible_ ? 1 : 0);
    sh.set("uPointPos", exitPosition());
    sh.set("uPointColor", glm::vec3(0, 1, 1) * kExitIntensity);
    sh.set("uPointRange", kExitRange);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadowSpot_ >= 0 ? shadowTex_ : ctx_.gfx.dummyShadow);

    const bool useNormals = pi.normalMaps && ctx_.normalMaps;
    auto bindSurface = [&](const Material& mat) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mat.color ? mat.color : ctx_.gfx.white);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mat.normal ? mat.normal : ctx_.gfx.flatNormal);
        sh.set("uUseNormal", useNormals && mat.normal ? 1 : 0);
    };

    sh.set("uModel", glm::mat4(1.0f));
    sh.set("uTint", glm::vec3(1.0f));
    sh.set("uSpecular", 0.1f);

    const float tiling = static_cast<float>(m.mazeTextureTiling);
    bindSurface(mats_[kWall]);
    sh.set("uWorldUv", 0);
    sh.set("uUvScale", glm::vec2(tiling, tiling * 1.5f));
    walls_.draw();

    sh.set("uWorldUv", 1);
    sh.set("uUvScale", glm::vec2(tiling / kMazeCell));
    bindSurface(mats_[kFloor]);
    floor_.draw();
    bindSurface(mats_[kCeiling]);
    ceiling_.draw();

    // Frames: dark matte (0x1a110a), untextured.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx_.gfx.white);
    sh.set("uUseNormal", 0);
    sh.set("uWorldUv", 0);
    sh.set("uUvScale", glm::vec2(1.0f));
    sh.set("uTint", srgbToLinear(hexColor(0x1a110a)));
    sh.set("uSpecular", 0.05f);
    frames_.draw();
    ctx_.gfx.drawCalls += 4;
    glDepthMask(GL_TRUE);

    // Paintings: one draw each, skipped beyond the fog.
    sh.set("uTint", glm::vec3(1.0f));
    sh.set("uSpecular", 0.0f);
    const float far = cam.farZ;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        for (const auto& p : paintings_) {
            if (p.tex->hasAlpha != (pass == 1)) continue;
            if (glm::distance(cam.pos, p.pos) > far) continue;
            glm::mat4 model = glm::rotate(glm::translate(glm::mat4(1.0f), p.pos), p.rotY, {0, 1, 0});
            model = glm::scale(glm::translate(model, {0, 0, 0.41f}), {p.w, p.h, 1.0f});
            sh.set("uModel", model);
            glBindTexture(GL_TEXTURE_2D, p.tex->id);
            ctx_.gfx.quad.draw();
            ++ctx_.gfx.drawCalls;
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Spotlight housings and lenses (only for lights in use).
    glDisable(GL_CULL_FACE);
    const glm::mat4 cylToZ = glm::rotate(glm::mat4(1.0f), kPi / 2, {1, 0, 0});
    for (int i = 0; i < nSpots; ++i) {
        const glm::mat4 basis = lookAtBasis(sPos[i], sPos[i] + sDir[i]);
        ctx_.gfx.drawUnlit(ctx_.gfx.housing, basis * cylToZ, cam, glm::vec4(hexColor(0x222222), 1.0f), fog);
        const glm::vec3 lensCol = glm::min(sCol[i] / std::max(intensity, 1.0f), glm::vec3(1.0f));
        ctx_.gfx.drawUnlit(ctx_.gfx.lens, glm::translate(basis, {0, 0, 1.0f}) * cylToZ, cam,
                           glm::vec4(glm::pow(lensCol, glm::vec3(1.0f / 2.2f)), 1.0f), fog);
    }

    // Exit crystal.
    if (exitVisible_) {
        const glm::vec3 ep = exitPosition();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), {ep.x, kEyeHeight + std::sin(time_ * 2.0f) * 1.5f, ep.z});
        model *= glm::mat4_cast(quatXYZ(0, exitSpinY_, exitSpinZ_));
        ctx_.gfx.drawUnlit(ctx_.gfx.octaWire, model, cam, glm::vec4(0.3f, 1.0f, 1.0f, 1.0f), fog);
        ctx_.gfx.drawUnlit(ctx_.gfx.octa, model, cam, glm::vec4(1.0f), fog);
    }
}

void MazeMode::renderOverlay() {
    if (!built_) return;
    const int W = ctx_.screenW, H = ctx_.screenH;
    auto& gfx = ctx_.gfx;
    if (ctx_.cfg.cur().showMazeMap) {
        const float k = H / 1080.0f;
        const float size = 200.0f * k, x0 = W - 20.0f * k - size, y0 = 20.0f * k, b = 2.0f * k;
        const glm::vec4 border(1, 1, 1, 0.2f);
        gfx.drawRect2D(x0 - b, y0 - b, size + 2 * b, b, border, W, H);
        gfx.drawRect2D(x0 - b, y0 + size, size + 2 * b, b, border, W, H);
        gfx.drawRect2D(x0 - b, y0, b, size, border, W, H);
        gfx.drawRect2D(x0 + size, y0, b, size, border, W, H);
        gfx.drawRect2D(x0, y0, size, size, {0, 0, 0, 0.4f}, W, H);
        const glm::mat4 mapXf = glm::scale(glm::translate(glm::mat4(1.0f), {x0, y0, 0}), {size, size, 1});
        gfx.drawMesh2D(mapWalls_, mapXf, {120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 0.8f}, W, H);
        const float cell = size / grid_.size();
        gfx.drawRect2D(x0 + exitCol_ * cell, y0 + exitRow_ * cell, cell, cell, {0, 1, 1, 0.8f}, W, H);

        const glm::vec3 fwd = ctx_.cam.forward();
        const float theta = std::atan2(fwd.x, -fwd.z);  // 0 = north (up on the map), clockwise positive
        const float mx = x0 + (ctx_.cam.pos.x / kMazeCell + 0.5f) * cell;
        const float my = y0 + (ctx_.cam.pos.z / kMazeCell + 0.5f) * cell;
        glm::mat4 a = glm::translate(glm::mat4(1.0f), {mx, my, 0});
        a = glm::rotate(a, theta, {0, 0, 1});
        a = glm::scale(a, {cell, cell, 1});
        gfx.drawMesh2D(arrow_, a, {1.0f, 0x44 / 255.0f, 0x44 / 255.0f, 1.0f}, W, H);
    }
    if (fade_ > 0.0f) gfx.drawRect2D(0, 0, static_cast<float>(W), static_cast<float>(H), {0, 0, 0, fade_}, W, H);
}

}  // namespace it
