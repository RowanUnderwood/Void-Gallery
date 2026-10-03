#include "modes/CardBatch.h"

#include <algorithm>

#include "modes/Mode.h"
#include "modes/MovingLights.h"

namespace it {

void CardBatch::add(const gl::Mesh* mesh, const glm::mat4& model, const glm::mat3& uvXform, const GpuTexture* tex,
                    const glm::mat4& view, float opacity) {
    const float depth = -(view * model[3]).z;
    if (depth < -100.0f) return;  // well behind the camera
    Item it{mesh, model, uvXform, tex, depth};
    if (tex->hasAlpha || opacity < 0.999f) blended_.push_back(it);
    else opaque_.push_back(it);
}

void CardBatch::draw(ModeContext& ctx) {
    auto& sh = ctx.gfx.card;
    const ModeSettings& s = ctx.cfg.cur();
    const float opacity = static_cast<float>(ctx.cfg.opacity);
    const float fog = static_cast<float>(s.fogDensity);

    sh.use();
    sh.set("uView", ctx.cam.view());
    sh.set("uProj", ctx.cam.proj());
    sh.set("uAmbient", glm::vec3(static_cast<float>(s.ambientIntensity)));
    sh.set("uFogDensity", fog);
    sh.set("uOpacity", opacity);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);  // cards are double-sided

    const auto& lights = ctx.lights.lights();
    const float intensity = static_cast<float>(s.lightIntensity);
    const int maxLights = std::min(ctx.maxLightsPerDraw, caps::kMaxLightsPerDraw);

    auto drawItem = [&](const Item& it) {
        int idx[caps::kMaxLightsPerDraw];
        const int n = ctx.lights.nearest(glm::vec3(it.model[3]), maxLights, idx);
        glm::vec3 pos[4], col[4];
        float range[4];
        for (int i = 0; i < n; ++i) {
            pos[i] = lights[idx[i]].pos;
            col[i] = lights[idx[i]].linear * intensity;
            range[i] = MovingLights::kRange;
        }
        sh.set("uNumLights", n);
        if (n > 0) {
            sh.setArray("uLightPos", pos, n);
            sh.setArray("uLightColor", col, n);
            sh.setArray("uLightRange", range, n);
        }
        sh.set("uModel", it.model);
        sh.set("uUvXform", it.uv);
        glBindTexture(GL_TEXTURE_2D, it.tex->id);
        it.mesh->draw();
        ++ctx.gfx.drawCalls;
    };

    std::sort(opaque_.begin(), opaque_.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    sh.set("uAlphaCut", 0.0f);
    for (const auto& it : opaque_) drawItem(it);

    if (!blended_.empty()) {
        std::sort(blended_.begin(), blended_.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        sh.set("uAlphaCut", 0.004f);  // skip fully transparent texels (saves blending bandwidth)
        for (const auto& it : blended_) drawItem(it);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

}  // namespace it
