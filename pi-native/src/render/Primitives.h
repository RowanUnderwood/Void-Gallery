#pragma once
// Geometry builders. Shapes match their three.js counterparts where the web version used them.

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "render/Gl.h"

namespace it::prim {

struct Geometry {
    std::vector<gl::Vertex> verts;
    std::vector<uint32_t> idx;
    void append(const Geometry& g, const glm::mat4& xform);
};

// Unit plane in XY centred at origin, normal +Z, uv (0,0) bottom-left (PlaneGeometry(1,1)).
Geometry plane(float w = 1.0f, float h = 1.0f);

// Axis-aligned quad from 4 corners (counter-clockwise when viewed from the front).
void appendQuad(Geometry& g, const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                const glm::vec3& n, const glm::vec4& tan, const glm::vec2& uvScale = {1, 1});

// Box centred at origin; `skipBack` omits the -Z face (used for frames sunk into a wall).
Geometry box(const glm::vec3& size, bool skipBack = false);

// CylinderGeometry(r, r, height, radialSegments, 1, openEnded=true, thetaStart, thetaLength).rotateX(PI/2),
// with U normalised to 0..1 (as setupTunnelGrid() does).
Geometry tunnelSegment(float radius, float height, int radialSegments, float thetaStart, float thetaLength);

// Closed cylinder along +Y.
Geometry cylinder(float radius, float height, int segments);

Geometry sphere(float radius, int widthSegments, int heightSegments);

Geometry octahedron(float radius);
Geometry octahedronEdges(float radius);  // for GL_LINES

}  // namespace it::prim
