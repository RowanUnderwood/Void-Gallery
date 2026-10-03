#pragma once
// Shared GPU resources: the four shader programs and the meshes every mode reuses.

#include "render/Gl.h"
#include "render/MathUtil.h"

namespace it {

struct Renderer {
    gl::Shader card;
    gl::Shader maze;
    gl::Shader unlit;
    gl::Shader depth;
    gl::Shader mazeDepth;
    gl::Shader present;

    gl::Mesh quad;        // unit plane, normal +Z
    gl::Mesh sphere;      // light orbs: SphereGeometry(1, 8, 8)
    gl::Mesh housing;     // spotlight housing: CylinderGeometry(0.8, 0.8, 2, 16)
    gl::Mesh lens;        // spotlight lens: CylinderGeometry(0.7, 0.7, 0.2, 16)
    gl::Mesh octa;        // exit crystal inner: OctahedronGeometry(1.5)
    gl::Mesh octaWire;    // exit crystal outer: OctahedronGeometry(3), wireframe
    gl::Mesh screenQuad;  // [0,1]^2 quad for 2D overlays

    GLuint white = 0;        // 1x1 white texture
    GLuint flatNormal = 0;   // 1x1 (0.5, 0.5, 1) normal
    GLuint dummyShadow = 0;  // 1x1 depth texture so the shadow sampler is always complete

    int drawCalls = 0;
    glm::mat4 overlayRotation{1.0f};  // applied to 2D overlays when the scene is rotated in clip space

    bool init();
    void shutdown();

    // Draws `mesh` unlit with an sRGB colour (+fog).
    void drawUnlit(const gl::Mesh& mesh, const glm::mat4& model, const Camera& cam, const glm::vec4& srgba,
                   float fogDensity);
    // 2D overlay in pixels (origin top-left).
    void drawRect2D(float x, float y, float w, float h, const glm::vec4& srgba, int screenW, int screenH);
    void drawMesh2D(const gl::Mesh& mesh, const glm::mat4& model, const glm::vec4& srgba, int screenW, int screenH);
    // Copies the composed logical frame onto the (bound) window framebuffer, rotated.
    void drawRotated(GLuint texture, int rotation);
};

}  // namespace it
