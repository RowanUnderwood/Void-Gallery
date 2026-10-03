#include "modes/MazeGen.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace it {

void MazeGrid::generate(int size, std::mt19937& rng) {
    const int s = (size % 2 == 0) ? size + 1 : size;
    size_ = s;
    cells_.assign(static_cast<size_t>(s) * s, 1);
    auto at = [&](int r, int c) -> uint8_t& { return cells_[static_cast<size_t>(r) * s + c]; };
    at(1, 1) = 0;
    std::vector<std::pair<int, int>> stack{{1, 1}};
    while (!stack.empty()) {
        const auto [r, c] = stack.back();
        std::pair<int, int> nbrs[4];
        int n = 0;
        const int cand[4][2] = {{r - 2, c}, {r + 2, c}, {r, c - 2}, {r, c + 2}};
        for (const auto& p : cand) {
            if (p[0] > 0 && p[0] < s - 1 && p[1] > 0 && p[1] < s - 1 && at(p[0], p[1]) == 1) nbrs[n++] = {p[0], p[1]};
        }
        if (n > 0) {
            const auto [nr, nc] = nbrs[std::uniform_int_distribution<int>(0, n - 1)(rng)];
            at((r + nr) / 2, (c + nc) / 2) = 0;  // carve passage
            at(nr, nc) = 0;
            stack.push_back({nr, nc});
        } else {
            stack.pop_back();
        }
    }
}

std::vector<WallFace> MazeGrid::wallFaces() const {
    std::vector<WallFace> faces;
    struct D { char f; int dr, dc; };
    const D dirs[4] = {{'N', -1, 0}, {'S', 1, 0}, {'W', 0, -1}, {'E', 0, 1}};
    for (int r = 0; r < size_; ++r)
        for (int c = 0; c < size_; ++c) {
            if (!wall(r, c)) continue;
            for (const auto& d : dirs) {
                const int nr = r + d.dr, nc = c + d.dc;
                if (nr >= 0 && nr < size_ && nc >= 0 && nc < size_ && !wall(nr, nc))
                    faces.push_back({r, c, d.f, d.dr, d.dc});
            }
        }
    return faces;
}

bool MazeGrid::lineOfSight(float x0, float z0, float x1, float z1) const {
    // Grid space: cell (r, c) spans [c - 0.5, c + 0.5] * kMazeCell; shift by 0.5 so cells are [c, c+1].
    const float gx0 = x0 / kMazeCell + 0.5f, gz0 = z0 / kMazeCell + 0.5f;
    const float gx1 = x1 / kMazeCell + 0.5f, gz1 = z1 / kMazeCell + 0.5f;
    int cx = static_cast<int>(std::floor(gx0)), cz = static_cast<int>(std::floor(gz0));
    const int ex = static_cast<int>(std::floor(gx1)), ez = static_cast<int>(std::floor(gz1));
    const float dx = gx1 - gx0, dz = gz1 - gz0;
    const float inf = std::numeric_limits<float>::infinity();
    const int stepX = dx > 0 ? 1 : -1, stepZ = dz > 0 ? 1 : -1;
    const float tDeltaX = dx != 0 ? std::abs(1.0f / dx) : inf;
    const float tDeltaZ = dz != 0 ? std::abs(1.0f / dz) : inf;
    float tMaxX = dx > 0 ? (cx + 1 - gx0) / dx : dx < 0 ? (gx0 - cx) / -dx : inf;
    float tMaxZ = dz > 0 ? (cz + 1 - gz0) / dz : dz < 0 ? (gz0 - cz) / -dz : inf;
    for (int guard = 0; guard < 4 * (size_ + 2); ++guard) {
        if (wall(cz, cx)) return false;
        if (cx == ex && cz == ez) return true;
        if (tMaxX < tMaxZ) {
            if (tMaxX > 1.0f) return true;
            tMaxX += tDeltaX;
            cx += stepX;
        } else {
            if (tMaxZ > 1.0f) return true;
            tMaxZ += tDeltaZ;
            cz += stepZ;
        }
    }
    return true;
}

void MazeGrid::startAndExit(int& startIdx, int& exitRow, int& exitCol) const {
    const int center = size_ / 2;
    startIdx = center % 2 == 0 ? center + 1 : center;
    int maxDist = -1;
    exitRow = exitCol = startIdx;
    for (int r = 1; r < size_ - 1; r += 2)
        for (int c = 1; c < size_ - 1; c += 2) {
            if (wall(r, c)) continue;
            const int d = std::abs(r - startIdx) + std::abs(c - startIdx);
            if (d > maxDist) {
                maxDist = d;
                exitRow = r;
                exitCol = c;
            }
        }
}

bool MazeGrid::canMove(int r, int c, int dr, int dc) const {
    const int pr = r + dr, pc = c + dc, nr = r + dr * 2, nc = c + dc * 2;
    if (pr < 0 || pr >= size_ || pc < 0 || pc >= size_ || nr < 0 || nr >= size_ || nc < 0 || nc >= size_) return false;
    return !wall(pr, pc) && !wall(nr, nc);
}

}  // namespace it
