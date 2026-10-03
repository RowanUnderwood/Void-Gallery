#include "render/Gl.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "assets/Decoder.h"

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace it::gl {

bool hasExtension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (e && std::strcmp(e, name) == 0) return true;
    }
    return false;
}

float maxAnisotropy() {
    static float cached = -1.0f;
    if (cached < 0.0f) {
        cached = 1.0f;
        if (hasExtension("GL_EXT_texture_filter_anisotropic")) glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &cached);
    }
    return cached;
}

// ---------------------------------------------------------------- Shader
Shader::~Shader() {
    if (prog_) glDeleteProgram(prog_);
}

static GLuint compileStage(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader] %s %s compile error:\n%s\n", name,
                     type == GL_VERTEX_SHADER ? "VS" : "FS", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Shader::build(const char* name, const char* vs, const char* fs) {
    GLuint v = compileStage(GL_VERTEX_SHADER, vs, name);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, name);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return false;
    }
    prog_ = glCreateProgram();
    glAttachShader(prog_, v);
    glAttachShader(prog_, f);
    glLinkProgram(prog_);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader] %s link error:\n%s\n", name, log);
        glDeleteProgram(prog_);
        prog_ = 0;
        return false;
    }
    return true;
}

GLint Shader::loc(const char* name) {
    auto it = locs_.find(name);
    if (it != locs_.end()) return it->second;
    const GLint l = glGetUniformLocation(prog_, name);
    locs_.emplace(name, l);
    return l;
}

// ---------------------------------------------------------------- Mesh
Mesh::~Mesh() { release(); }

Mesh& Mesh::operator=(Mesh&& o) noexcept {
    if (this != &o) {
        release();
        vao_ = o.vao_;
        vbo_ = o.vbo_;
        ibo_ = o.ibo_;
        count_ = o.count_;
        prim_ = o.prim_;
        o.vao_ = o.vbo_ = o.ibo_ = 0;
        o.count_ = 0;
    }
    return *this;
}

void Mesh::release() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    vao_ = vbo_ = ibo_ = 0;
    count_ = 0;
}

void Mesh::upload(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx, GLenum prim, bool dynamic) {
    prim_ = prim;
    const GLenum usage = dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    if (!vao_) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ibo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        const GLsizei stride = sizeof(Vertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, pos)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, nrm)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, uv)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, tan)));
    } else {
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    }
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(), usage);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)), idx.data(), usage);
    glBindVertexArray(0);
    count_ = static_cast<GLsizei>(idx.size());
}

void Mesh::draw() const {
    if (!count_) return;
    glBindVertexArray(vao_);
    glDrawElements(prim_, count_, GL_UNSIGNED_INT, nullptr);
}

// ---------------------------------------------------------------- Textures
void setAnisotropy(GLuint tex, int level) {
    const float maxA = maxAnisotropy();
    if (maxA <= 1.0f) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::clamp(static_cast<float>(level), 1.0f, maxA));
}

GLuint createTexture(const DecodedImage& img, const TextureOptions& opt) {
    if (img.width <= 0 || img.height <= 0) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const int levels = opt.mipmaps
                           ? 1 + static_cast<int>(std::floor(std::log2(std::max(img.width, img.height))))
                           : 1;
    glTexStorage2D(GL_TEXTURE_2D, levels, opt.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, img.width, img.height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    if (opt.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, opt.mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const GLint wrap = opt.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    if (opt.anisotropy > 1) setAnisotropy(tex, opt.anisotropy);
    return tex;
}

GLuint createSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    DecodedImage img;
    img.width = img.height = 1;
    img.rgba = {r, g, b, a};
    return createTexture(img, TextureOptions{false, false, true, 1});
}

// ---------------------------------------------------------------- SceneTarget
SceneTarget::~SceneTarget() {
    destroyScene();
    destroyPresent();
}

void SceneTarget::destroyScene() {
    if (msFbo_) glDeleteFramebuffers(1, &msFbo_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    GLuint rbs[] = {msColor_, msDepth_, color_, depth_};
    for (GLuint rb : rbs)
        if (rb) glDeleteRenderbuffers(1, &rb);
    msFbo_ = msColor_ = msDepth_ = fbo_ = color_ = depth_ = 0;
    w_ = h_ = samples_ = 0;
}

void SceneTarget::destroyPresent() {
    if (presentFbo_) glDeleteFramebuffers(1, &presentFbo_);
    if (presentTex_) glDeleteTextures(1, &presentTex_);
    if (presentDepth_) glDeleteRenderbuffers(1, &presentDepth_);
    presentFbo_ = presentTex_ = presentDepth_ = 0;
    pw_ = ph_ = 0;
}

static GLuint makeRenderbuffer(GLenum fmt, int samples, int w, int h) {
    GLuint rb = 0;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    if (samples > 0) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt, w, h);
    else glRenderbufferStorage(GL_RENDERBUFFER, fmt, w, h);
    return rb;
}

void SceneTarget::bindOverlaySurface() {
    glBindFramebuffer(GL_FRAMEBUFFER, rotated_ ? presentFbo_ : 0);
    glViewport(0, 0, lw_, lh_);
}

void SceneTarget::begin(int lw, int lh, float scale, int msaa, bool rotated) {
    lw_ = lw;
    lh_ = lh;
    rotated_ = rotated;

    // Logical-size texture the whole frame is composed into when the monitor is rotated. It has a
    // depth buffer so the scene can render straight into it when no scaling/MSAA is used.
    if (rotated && (pw_ != lw || ph_ != lh || !presentFbo_)) {
        destroyPresent();
        pw_ = lw;
        ph_ = lh;
        glGenTextures(1, &presentTex_);
        glBindTexture(GL_TEXTURE_2D, presentTex_);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, lw, lh);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &presentFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, presentFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, presentTex_, 0);
        presentDepth_ = makeRenderbuffer(GL_DEPTH_COMPONENT24, 0, lw, lh);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, presentDepth_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "[gl] rotation framebuffer incomplete (%dx%d)\n", lw, lh);
    } else if (!rotated && presentFbo_) {
        destroyPresent();
    }

    const bool scaled = scale < 0.999f;
    direct_ = !scaled && msaa <= 0;
    if (direct_) {
        bindOverlaySurface();
        return;
    }
    const int w = std::max(1, static_cast<int>(std::lround(lw * (scaled ? scale : 1.0f))));
    const int h = std::max(1, static_cast<int>(std::lround(lh * (scaled ? scale : 1.0f))));
    const int samples = msaa > 0 ? msaa : 0;
    if (w != w_ || h != h_ || samples != samples_ || !fbo_) {
        destroyScene();
        w_ = w;
        h_ = h;
        samples_ = samples;
        if (samples > 0) {
            glGenFramebuffers(1, &msFbo_);
            glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
            msColor_ = makeRenderbuffer(GL_RGBA8, samples, w, h);
            msDepth_ = makeRenderbuffer(GL_DEPTH_COMPONENT24, samples, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
        }
        // Single-sample RGBA8 target for scaling and for every MSAA resolve: GLES 3 requires
        // identical formats when blitting from a multisampled buffer, and the window's may differ.
        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        color_ = makeRenderbuffer(GL_RGBA8, 0, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_);
        if (samples == 0) {
            depth_ = makeRenderbuffer(GL_DEPTH_COMPONENT24, 0, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
        }
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "[gl] scene framebuffer incomplete (%dx%d, %d samples)\n", w, h, samples);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, samples > 0 ? msFbo_ : fbo_);
    glViewport(0, 0, w_, h_);
}

void SceneTarget::endScene() {
    if (!direct_) {
        if (samples_ > 0) {  // resolve into the RGBA8 target (same size, same format)
            glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
            glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rotated_ ? presentFbo_ : 0);
        glBlitFramebuffer(0, 0, w_, h_, 0, 0, lw_, lh_, GL_COLOR_BUFFER_BIT,
                          (w_ == lw_ && h_ == lh_) ? GL_NEAREST : GL_LINEAR);
    }
    bindOverlaySurface();
}

}  // namespace it::gl
