#pragma once
// Thin OpenGL ES 3.1 helpers: shaders, meshes, textures and the scaled/MSAA scene target.

#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace it {

struct DecodedImage;

namespace gl {

bool hasExtension(const char* name);
float maxAnisotropy();  // 1 if unsupported

class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool build(const char* name, const char* vs, const char* fs);
    void use() const { glUseProgram(prog_); }
    GLint loc(const char* name);

    void set(const char* n, int v) { glUniform1i(loc(n), v); }
    void set(const char* n, float v) { glUniform1f(loc(n), v); }
    void set(const char* n, const glm::vec2& v) { glUniform2fv(loc(n), 1, &v[0]); }
    void set(const char* n, const glm::vec3& v) { glUniform3fv(loc(n), 1, &v[0]); }
    void set(const char* n, const glm::vec4& v) { glUniform4fv(loc(n), 1, &v[0]); }
    void set(const char* n, const glm::mat3& v) { glUniformMatrix3fv(loc(n), 1, GL_FALSE, &v[0][0]); }
    void set(const char* n, const glm::mat4& v) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, &v[0][0]); }
    void setArray(const char* n, const glm::vec3* v, int count) { glUniform3fv(loc(n), count, &v[0][0]); }
    void setArray(const char* n, const float* v, int count) { glUniform1fv(loc(n), count, v); }

private:
    GLuint prog_ = 0;
    std::unordered_map<std::string, GLint> locs_;
};

// Interleaved vertex layout shared by every mesh. Attribute locations: 0 pos, 1 normal, 2 uv, 3 tangent.
struct Vertex {
    glm::vec3 pos{0.0f};
    glm::vec3 nrm{0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 tan{1.0f, 0.0f, 0.0f, 1.0f};  // xyz tangent, w bitangent sign
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept { *this = std::move(o); }
    Mesh& operator=(Mesh&& o) noexcept;

    void upload(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx,
                GLenum prim = GL_TRIANGLES, bool dynamic = false);
    void draw() const;
    void release();
    bool empty() const { return count_ == 0; }

private:
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
    GLsizei count_ = 0;
    GLenum prim_ = GL_TRIANGLES;
};

struct TextureOptions {
    bool srgb = true;
    bool mipmaps = true;
    bool repeat = false;
    int anisotropy = 1;
};

GLuint createTexture(const DecodedImage& img, const TextureOptions& opt);
GLuint createSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void setAnisotropy(GLuint tex, int level);

// Frame composition:
//   3D scene -> [scaled and/or MSAA buffer] -> "overlay surface" (logical size) -> window
// The overlay surface is the window itself, or, when the monitor is rotated, an offscreen
// logical-size texture that is drawn rotated onto the window by the caller (see presentTexture()).
class SceneTarget {
public:
    ~SceneTarget();
    // Binds the target for the 3D scene. logicalW/H is the (possibly portrait) virtual screen.
    void begin(int logicalW, int logicalH, float scale, int msaa, bool rotated);
    // Resolves/scales the scene into the overlay surface and binds it (viewport = logical size).
    void endScene();
    GLuint presentTexture() const { return presentTex_; }
    int width() const { return direct_ ? lw_ : w_; }
    int height() const { return direct_ ? lh_ : h_; }

private:
    void destroyScene();
    void destroyPresent();
    void bindOverlaySurface();
    bool direct_ = true;
    bool rotated_ = false;
    int lw_ = 0, lh_ = 0;
    int w_ = 0, h_ = 0, samples_ = 0;
    GLuint msFbo_ = 0, msColor_ = 0, msDepth_ = 0;          // multisampled render buffers
    GLuint fbo_ = 0, color_ = 0, depth_ = 0;                // single-sample target (resolve / scale)
    GLuint presentFbo_ = 0, presentTex_ = 0, presentDepth_ = 0;
    int pw_ = 0, ph_ = 0;
};

}  // namespace gl
}  // namespace it
