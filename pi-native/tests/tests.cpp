// Minimal self-contained unit tests (no GL context needed).

#include <cmath>
#include <cstdio>
#include <deque>
#include <random>
#include <vector>

#include "assets/Decoder.h"
#include "config/Config.h"
#include "modes/MazeGen.h"
#include "modes/TunnelUv.h"

static int gFailures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++gFailures;                                                     \
        }                                                                    \
    } while (0)

using namespace it;

static void testMazeConnected() {
    std::mt19937 rng(1234);
    for (int size : {11, 12, 21, 31}) {
        MazeGrid g;
        g.generate(size, rng);
        CHECK(g.size() % 2 == 1);
        // Every odd cell must be reachable from (1,1).
        const int n = g.size();
        std::vector<char> seen(static_cast<size_t>(n) * n, 0);
        std::deque<std::pair<int, int>> q{{1, 1}};
        seen[n + 1] = 1;
        while (!q.empty()) {
            auto [r, c] = q.front();
            q.pop_front();
            const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& dd : d) {
                const int nr = r + dd[0], nc = c + dd[1];
                if (!g.wall(nr, nc) && !seen[static_cast<size_t>(nr) * n + nc]) {
                    seen[static_cast<size_t>(nr) * n + nc] = 1;
                    q.push_back({nr, nc});
                }
            }
        }
        for (int r = 1; r < n - 1; r += 2)
            for (int c = 1; c < n - 1; c += 2) CHECK(!g.wall(r, c) && seen[static_cast<size_t>(r) * n + c]);
        int s, er, ec;
        g.startAndExit(s, er, ec);
        CHECK(!g.wall(s, s));
        CHECK(!g.wall(er, ec));
        // A dead end never traps the navigator: every open odd cell can move somewhere.
        for (int r = 1; r < n - 1; r += 2)
            for (int c = 1; c < n - 1; c += 2) {
                const bool any = g.canMove(r, c, 1, 0) || g.canMove(r, c, -1, 0) || g.canMove(r, c, 0, 1) ||
                                 g.canMove(r, c, 0, -1);
                CHECK(any);
            }
    }
}

// Brute-force reference: sample the segment densely.
static bool losReference(const MazeGrid& g, float x0, float z0, float x1, float z1) {
    const int steps = 2000;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const float x = x0 + (x1 - x0) * t, z = z0 + (z1 - z0) * t;
        const int c = static_cast<int>(std::floor(x / kMazeCell + 0.5f));
        const int r = static_cast<int>(std::floor(z / kMazeCell + 0.5f));
        if (g.wall(r, c)) return false;
    }
    return true;
}

static void testLineOfSight() {
    std::mt19937 rng(99);
    MazeGrid g;
    g.generate(21, rng);
    std::uniform_int_distribution<int> cell(0, 9);
    std::uniform_real_distribution<float> jitter(-6.0f, 6.0f);
    int mismatches = 0, total = 0;
    for (int i = 0; i < 3000; ++i) {
        const int r0 = cell(rng) * 2 + 1, c0 = cell(rng) * 2 + 1, r1 = cell(rng) * 2 + 1, c1 = cell(rng) * 2 + 1;
        const float x0 = c0 * kMazeCell + jitter(rng), z0 = r0 * kMazeCell + jitter(rng);
        const float x1 = c1 * kMazeCell + jitter(rng), z1 = r1 * kMazeCell + jitter(rng);
        ++total;
        if (g.lineOfSight(x0, z0, x1, z1) != losReference(g, x0, z0, x1, z1)) ++mismatches;
    }
    // Sampling can miss corner grazes; DDA is exact. Allow a tiny disagreement rate.
    CHECK(mismatches * 200 < total);
}

static void testTunnelUv() {
    // The affine matrix must reproduce the per-vertex mapping for every rotation/column.
    const float seg = 6.2831853f / 12.0f;
    for (int rot : {0, 90, 180, 270})
        for (int col = 0; col < 12; ++col)
            for (float ratio : {0.6f, 1.0f, 1.7f}) {
                int r;
                float sx, sy;
                const glm::mat3 m = tunnelUvTransform(ratio, col, seg, seg * 85.0f, 60.0f, rot, &r, &sx, &sy);
                for (glm::vec2 uv : {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0, 1), glm::vec2(1, 1),
                                     glm::vec2(0.3f, 0.8f)}) {
                    const glm::vec2 a = glm::vec2(m * glm::vec3(uv, 1.0f));
                    const glm::vec2 b = tunnelUvMap(uv, r, sx, sy);
                    CHECK(std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f);
                }
            }
}

static void testConfigRoundTrip() {
    Config c;
    json doc = json::parse(R"({
      "version": 2.2, "activeMode": "grid",
      "globals": {"serverPath": "movieposters", "quality": "half", "useStereo": true, "custom": 7},
      "modes": {"grid": {"gridCols": 40, "stereoEyeSep": 0.8, "cameraSpeed": 0.25},
                "maze": {"mazeComplexity": 22, "navStyle": "modern"}}
    })");
    c.apply(doc);
    c.clampForPi();
    CHECK(c.activeMode == "grid");
    CHECK(c.globals.serverPath == "movieposters");
    CHECK(c.modes["grid"].gridCols == caps::kMaxGridDim);
    CHECK(std::abs(c.modes["grid"].cameraSpeed - 0.25) < 1e-9);
    CHECK(c.modes["maze"].mazeComplexity % 2 == 1);
    CHECK(c.modes["maze"].navStyle == "modern");
    const json out = c.toJson();
    CHECK(out["globals"]["useStereo"] == true);        // unknown keys preserved
    CHECK(out["globals"]["custom"] == 7);
    CHECK(out["modes"]["grid"]["stereoEyeSep"] == 0.8);
    CHECK(std::abs(out["version"].get<double>() - 2.2) < 1e-9);
    Config c2;
    c2.apply(out);
    CHECK(c2.modes["grid"].gridCols == c.modes["grid"].gridCols);
    CHECK(c2.globals.quality == "half");
}

static void testDownscale() {
    std::vector<uint8_t> src(4 * 4 * 4, 0);
    for (int i = 0; i < 16; ++i) src[i * 4] = static_cast<uint8_t>(i < 8 ? 0 : 255), src[i * 4 + 3] = 255;
    std::vector<uint8_t> dst(2 * 2 * 4);
    downscaleRgba(src.data(), 4, 4, dst.data(), 2, 2);
    CHECK(dst[0] == 0 && dst[4] == 0);         // top rows black
    CHECK(dst[8] == 255 && dst[12] == 255);    // bottom rows red
    CHECK(dst[3] == 255);
}

int main() {
    testMazeConnected();
    testLineOfSight();
    testTunnelUv();
    testConfigRoundTrip();
    testDownscale();
    if (gFailures) {
        std::fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
