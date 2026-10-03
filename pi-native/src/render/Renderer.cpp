#include "render/Renderer.h"

#include "render/Primitives.h"
#include "render/ShaderSources.h"

namespace it {

namespace {
void uploadGeometry(gl::Mesh& m, const prim::Geometry& g, GLenum prim = GL_TRIANGLES) { m.upload(g.verts, g.idx, prim); }
}  // namespace

bool Renderer::init() {
    bool ok = card.build("card", shaders::kCardVS, shaders::kCardFS);
    ok &= maze.build("maze", shaders::kMazeVS, shaders::kMazeFS);
    ok &= unlit.build("unlit", shaders::kUnlitVS, shaders::kUnlitFS);
    ok &= depth.build("depth", shaders::kDepthVS, shaders::kDepthFS);
    ok &= mazeDepth.build("mazeDepth", shaders::kMazeDepthVS, shaders::kDepthFS);
    ok &= present.build("present", shaders::kPresentVS, shaders::kPresentFS);
    if (!ok) return false;
    present.use();
    present.set("uTex", 0);

    card.use();
    card.set("uTex", 0);
    maze.use();
    maze.set("uAlbedo", 0);
    maze.set("uNormalMap", 1);
    maze.set("uShadowMaps", 2);
    maze.set("uRoughMap", 3);

    uploadGeometry(quad, prim::plane());
    uploadGeometry(sphere, prim::sphere(1.0f, 8, 8));
    uploadGeometry(housing, prim::cylinder(0.8f, 2.0f, 16));
    uploadGeometry(lens, prim::cylinder(0.7f, 0.2f, 16));
    uploadGeometry(octa, prim::octahedron(1.5f));
    uploadGeometry(octaWire, prim::octahedronEdges(3.0f), GL_LINES);
    {
        prim::Geometry g;
        prim::appendQuad(g, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0, 1});
        uploadGeometry(screenQuad, g);
    }

    white = gl::createSolidTexture(255, 255, 255, 255);
    flatNormal = gl::createSolidTexture(128, 128, 255, 255);

    glGenTextures(1, &dummyShadow);
    glBindTexture(GL_TEXTURE_2D_ARRAY, dummyShadow);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT16, 1, 1, 1);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return true;
}

void Renderer::shutdown() {
    GLuint texs[] = {white, flatNormal, dummyShadow};
    glDeleteTextures(3, texs);
    white = flatNormal = dummyShadow = 0;
}

void Renderer::drawUnlit(const gl::Mesh& mesh, const glm::mat4& model, const Camera& cam, const glm::vec4& srgba,
                         float fogDensity) {
    const glm::mat4 mv = cam.view() * model;
    unlit.use();
    unlit.set("uMVP", cam.proj() * mv);
    unlit.set("uModelView", mv);
    unlit.set("uColor", srgba);
    unlit.set("uFogDensity", fogDensity);
    mesh.draw();
    ++drawCalls;
}

void Renderer::drawMesh2D(const gl::Mesh& mesh, const glm::mat4& model, const glm::vec4& srgba, int sw, int sh) {
    const glm::mat4 ortho =
        overlayRotation * glm::ortho(0.0f, static_cast<float>(sw), static_cast<float>(sh), 0.0f, -1.0f, 1.0f);
    unlit.use();
    unlit.set("uMVP", ortho * model);
    unlit.set("uModelView", glm::mat4(1.0f));
    unlit.set("uColor", srgba);
    unlit.set("uFogDensity", 0.0f);
    mesh.draw();
    ++drawCalls;
}

void Renderer::drawRotated(GLuint texture, int rotation, const glm::vec2& scale) {
    present.use();
    present.set("uRotation", rotation);
    present.set("uScale", scale);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    screenQuad.draw();
    ++drawCalls;
}

void Renderer::drawRect2D(float x, float y, float w, float h, const glm::vec4& srgba, int sw, int sh) {
    const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), {x, y, 0.0f}), {w, h, 1.0f});
    drawMesh2D(screenQuad, model, srgba, sw, sh);
}

}  // namespace it
