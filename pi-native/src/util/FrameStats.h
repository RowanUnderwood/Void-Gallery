#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace it {

// Rolling frame-time statistics (display interval, i.e. including the vsync wait).
class FrameStats {
public:
    static constexpr size_t kHistory = 240;

    void push(float frameMs, float cpuMs);
    float fps() const;
    float avgMs() const;
    float percentileMs(float p) const;  // over the history window
    float cpuMs() const { return cpu_; }
    const float* history() const { return hist_.data(); }
    size_t historyOffset() const { return head_; }
    size_t count() const { return count_; }

private:
    std::array<float, kHistory> hist_{};
    size_t head_ = 0;
    size_t count_ = 0;
    float cpu_ = 0.0f;
};

// Records every frame for --bench and writes a CSV + summary.
class BenchRecorder {
public:
    void start(const std::string& mode, float seconds, float refreshHz);
    bool active() const { return active_; }
    bool record(float frameMs, float cpuMs, size_t texMB, const std::string& throttle);  // false when done
    // Prints a summary; returns true if p99 <= 33.3 ms.
    bool finish(const std::string& csvPath) const;

private:
    struct Sample { float frameMs, cpuMs; size_t texMB; std::string throttle; };
    std::vector<Sample> samples_;
    std::string mode_;
    float seconds_ = 0, elapsed_ = 0, refreshHz_ = 60;
    bool active_ = false;
};

// Auto-quality: drops one quality level when p95 frame time stays above the budget.
class AutoQuality {
public:
    int level() const { return level_; }
    void reset() { level_ = 0; overFor_ = underFor_ = 0; }
    // Returns true if the level changed.
    bool update(float dt, float p95Ms, float budgetMs);
    static constexpr int kMaxLevel = 4;

private:
    int level_ = 0;
    float overFor_ = 0, underFor_ = 0;
};

}  // namespace it
