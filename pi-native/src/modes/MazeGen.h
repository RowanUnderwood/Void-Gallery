#pragma once
// Maze grid generation (recursive backtracker, as MazeGenerator in index.html) and the grid
// line-of-sight test that replaces the web version's per-frame raycasts.

#include <cstdint>
#include <random>
#include <vector>

namespace it {

constexpr float kMazeCell = 20.0f;  // MAZE_CELL_SIZE

struct WallFace {
    int row, col;  // wall cell
    char face;     // 'N' | 'S' | 'W' | 'E' (side of the wall cell facing the open neighbour)
    int dr, dc;    // direction from wall cell to the open neighbour
};

class MazeGrid {
public:
    void generate(int size, std::mt19937& rng);
    int size() const { return size_; }
    bool wall(int r, int c) const {
        return r < 0 || c < 0 || r >= size_ || c >= size_ || cells_[static_cast<size_t>(r) * size_ + c] != 0;
    }
    std::vector<WallFace> wallFaces() const;

    // True if the straight segment (world XZ) crosses no wall cell.
    bool lineOfSight(float x0, float z0, float x1, float z1) const;

    // Start = centre-ish odd cell; exit = open odd cell with greatest Manhattan distance.
    void startAndExit(int& startIdx, int& exitRow, int& exitCol) const;

    // True if a step of two cells in direction (dr, dc) from (r, c) stays in open corridor.
    bool canMove(int r, int c, int dr, int dc) const;

private:
    int size_ = 0;
    std::vector<uint8_t> cells_;  // 1 = wall
};

}  // namespace it
