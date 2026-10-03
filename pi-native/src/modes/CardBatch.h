#pragma once
// Collects image cards for a frame and draws them: opaque front-to-back with depth writes, then
// translucent back-to-front without (index.html blended every card; only real alpha needs it).

#include <vector>

#include <glm/glm.hpp>

#include "assets/TextureStreamer.h"
#include "render/Gl.h"

namespace it {

struct ModeContext;

class CardBatch {
public:
    void clear() {
        opaque_.clear();
        blended_.clear();
    }
    void add(const gl::Mesh* mesh, const glm::mat4& model, const glm::mat3& uvXform, const GpuTexture* tex,
             const glm::mat4& view, float opacity);
    void draw(ModeContext& ctx);

private:
    struct Item {
        const gl::Mesh* mesh;
        glm::mat4 model;
        glm::mat3 uv;
        const GpuTexture* tex;
        float depth;
    };
    std::vector<Item> opaque_;
    std::vector<Item> blended_;
};

}  // namespace it
