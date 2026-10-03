#include "render/Primitives.h"

#include <cmath>

#include "render/MathUtil.h"

namespace it::prim {

void Geometry::append(const Geometry& g, const glm::mat4& xform) {
    const uint32_t base = static_cast<uint32_t>(verts.size());
    const glm::mat3 nmat = glm::mat3(xform);
    for (const auto& v : g.verts) {
        gl::Vertex o = v;
        o.pos = glm::vec3(xform * glm::vec4(v.pos, 1.0f));
        o.nrm = glm::normalize(nmat * v.nrm);
        o.tan = glm::vec4(glm::normalize(nmat * glm::vec3(v.tan)), v.tan.w);
        verts.push_back(o);
    }
    for (uint32_t i : g.idx) idx.push_back(base + i);
}

void appendQuad(Geometry& g, const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                const glm::vec3& n, const glm::vec4& tan, const glm::vec2& s) {
    const uint32_t b = static_cast<uint32_t>(g.verts.size());
    g.verts.push_back({p0, n, {0, 0}, tan});
    g.verts.push_back({p1, n, {s.x, 0}, tan});
    g.verts.push_back({p2, n, {s.x, s.y}, tan});
    g.verts.push_back({p3, n, {0, s.y}, tan});
    g.idx.insert(g.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
}

Geometry plane(float w, float h) {
    Geometry g;
    const float x = w * 0.5f, y = h * 0.5f;
    appendQuad(g, {-x, -y, 0}, {x, -y, 0}, {x, y, 0}, {-x, y, 0}, {0, 0, 1}, {1, 0, 0, 1});
    return g;
}

Geometry box(const glm::vec3& size, bool skipBack) {
    Geometry g;
    const glm::vec3 h = size * 0.5f;
    // n, t (u direction), b (v direction) with t x b = n so every face is CCW from outside.
    struct Face { glm::vec3 n, t, b; float ht, hb, hn; };
    const Face faces[] = {
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, h.x, h.y, h.z},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, h.x, h.y, h.z},
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, h.z, h.y, h.x},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, h.z, h.y, h.x},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, h.x, h.z, h.y},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, h.x, h.z, h.y},
    };
    for (const auto& f : faces) {
        if (skipBack && f.n.z < -0.5f) continue;
        const glm::vec3 c = f.n * f.hn;
        appendQuad(g, c - f.t * f.ht - f.b * f.hb, c + f.t * f.ht - f.b * f.hb, c + f.t * f.ht + f.b * f.hb,
                   c - f.t * f.ht + f.b * f.hb, f.n, glm::vec4(f.t, 1.0f));
    }
    return g;
}

Geometry tunnelSegment(float radius, float height, int radialSegments, float thetaStart, float thetaLength) {
    Geometry g;
    const float half = height * 0.5f;
    for (int y = 0; y <= 1; ++y) {
        const float v = static_cast<float>(y);
        for (int x = 0; x <= radialSegments; ++x) {
            const float u = static_cast<float>(x) / radialSegments;
            const float th = u * thetaLength + thetaStart;
            const float s = std::sin(th), c = std::cos(th);
            // three.js torso vertex (r sin, -v h + h/2, r cos) then rotateX(PI/2): (x, y, z) -> (x, -z, y)
            const glm::vec3 p(radius * s, -radius * c, -v * height + half);
            gl::Vertex vert;
            vert.pos = p;
            vert.nrm = glm::vec3(s, -c, 0.0f);
            vert.uv = glm::vec2(u, 1.0f - v);
            vert.tan = glm::vec4(c, s, 0.0f, 1.0f);
            g.verts.push_back(vert);
        }
    }
    const uint32_t row = static_cast<uint32_t>(radialSegments + 1);
    for (uint32_t x = 0; x < static_cast<uint32_t>(radialSegments); ++x) {
        const uint32_t a = x, b = row + x, c = row + x + 1, d = x + 1;
        g.idx.insert(g.idx.end(), {a, b, d, b, c, d});
    }
    return g;
}

Geometry cylinder(float radius, float height, int segments) {
    Geometry g;
    const float half = height * 0.5f;
    for (int y = 0; y <= 1; ++y) {
        for (int x = 0; x <= segments; ++x) {
            const float th = 2.0f * kPi * x / segments;
            const float s = std::sin(th), c = std::cos(th);
            g.verts.push_back({{radius * s, y ? -half : half, radius * c}, {s, 0, c},
                               {static_cast<float>(x) / segments, 1.0f - y}, {c, 0, -s, 1}});
        }
    }
    const uint32_t row = static_cast<uint32_t>(segments + 1);
    for (uint32_t x = 0; x < static_cast<uint32_t>(segments); ++x) {
        const uint32_t a = x, b = row + x, c = row + x + 1, d = x + 1;
        g.idx.insert(g.idx.end(), {a, b, d, b, c, d});
    }
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap ? -half : half;
        const glm::vec3 n(0, cap ? -1.0f : 1.0f, 0);
        const uint32_t center = static_cast<uint32_t>(g.verts.size());
        g.verts.push_back({{0, y, 0}, n, {0.5f, 0.5f}, {1, 0, 0, 1}});
        for (int x = 0; x <= segments; ++x) {
            const float th = 2.0f * kPi * x / segments;
            g.verts.push_back({{radius * std::sin(th), y, radius * std::cos(th)}, n,
                               {0.5f + 0.5f * std::sin(th), 0.5f + 0.5f * std::cos(th)}, {1, 0, 0, 1}});
        }
        for (uint32_t x = 0; x < static_cast<uint32_t>(segments); ++x) {
            if (cap == 0) g.idx.insert(g.idx.end(), {center, center + 1 + x, center + 2 + x});
            else g.idx.insert(g.idx.end(), {center, center + 2 + x, center + 1 + x});
        }
    }
    return g;
}

Geometry sphere(float radius, int ws, int hs) {
    Geometry g;
    for (int y = 0; y <= hs; ++y) {
        const float v = static_cast<float>(y) / hs;
        for (int x = 0; x <= ws; ++x) {
            const float u = static_cast<float>(x) / ws;
            const glm::vec3 n(-std::cos(u * 2 * kPi) * std::sin(v * kPi), std::cos(v * kPi),
                              std::sin(u * 2 * kPi) * std::sin(v * kPi));
            g.verts.push_back({n * radius, n, {u, 1 - v}, {1, 0, 0, 1}});
        }
    }
    const uint32_t row = static_cast<uint32_t>(ws + 1);
    for (uint32_t y = 0; y < static_cast<uint32_t>(hs); ++y) {
        for (uint32_t x = 0; x < static_cast<uint32_t>(ws); ++x) {
            const uint32_t a = y * row + x + 1, b = y * row + x, c = (y + 1) * row + x, d = (y + 1) * row + x + 1;
            if (y != 0) g.idx.insert(g.idx.end(), {a, b, d});
            if (y != static_cast<uint32_t>(hs) - 1) g.idx.insert(g.idx.end(), {b, c, d});
        }
    }
    return g;
}

namespace {
const glm::vec3 kOctaVerts[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const uint32_t kOctaFaces[24] = {0, 2, 4, 0, 4, 3, 0, 3, 5, 0, 5, 2, 1, 2, 5, 1, 5, 3, 1, 3, 4, 1, 4, 2};
}  // namespace

Geometry octahedron(float r) {
    Geometry g;
    for (int f = 0; f < 8; ++f) {
        const glm::vec3 a = kOctaVerts[kOctaFaces[f * 3]] * r;
        const glm::vec3 b = kOctaVerts[kOctaFaces[f * 3 + 1]] * r;
        const glm::vec3 c = kOctaVerts[kOctaFaces[f * 3 + 2]] * r;
        const glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        const uint32_t base = static_cast<uint32_t>(g.verts.size());
        g.verts.push_back({a, n, {0, 0}, {1, 0, 0, 1}});
        g.verts.push_back({b, n, {1, 0}, {1, 0, 0, 1}});
        g.verts.push_back({c, n, {0, 1}, {1, 0, 0, 1}});
        g.idx.insert(g.idx.end(), {base, base + 1, base + 2});
    }
    return g;
}

Geometry octahedronEdges(float r) {
    Geometry g;
    for (const auto& v : kOctaVerts) g.verts.push_back({v * r, glm::normalize(v), {0, 0}, {1, 0, 0, 1}});
    // Every pair of vertices that are not opposite each other forms an edge.
    for (uint32_t i = 0; i < 6; ++i)
        for (uint32_t j = i + 1; j < 6; ++j)
            if (!(j == i + 1 && i % 2 == 0)) g.idx.insert(g.idx.end(), {i, j});
    return g;
}

}  // namespace it::prim
