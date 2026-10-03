#include "util/FrameStats.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace it {

void FrameStats::push(float frameMs, float cpuMs, float gpuMs) {
    hist_[head_] = frameMs;
    head_ = (head_ + 1) % kHistory;
    count_ = std::min(count_ + 1, kHistory);
    cpu_ = cpu_ * 0.9f + cpuMs * 0.1f;
    gpu_ = gpu_ * 0.9f + gpuMs * 0.1f;
}

float FrameStats::avgMs() const {
    if (!count_) return 0.0f;
    float s = 0;
    for (size_t i = 0; i < count_; ++i) s += hist_[i];
    return s / count_;
}

float FrameStats::fps() const {
    const float a = avgMs();
    return a > 0 ? 1000.0f / a : 0.0f;
}

float FrameStats::percentileMs(float p) const {
    if (!count_) return 0.0f;
    std::vector<float> v(hist_.begin(), hist_.begin() + count_);
    const size_t k = std::min(count_ - 1, static_cast<size_t>(p * (count_ - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

void BenchRecorder::start(const std::string& mode, float seconds, float refreshHz) {
    mode_ = mode;
    seconds_ = seconds;
    refreshHz_ = refreshHz > 0 ? refreshHz : 60.0f;
    elapsed_ = 0;
    samples_.clear();
    samples_.reserve(static_cast<size_t>(seconds * 70));
    active_ = true;
}

bool BenchRecorder::record(float frameMs, float cpuMs, float gpuMs, size_t texMB, const std::string& throttle) {
    if (!active_) return false;
    samples_.push_back({frameMs, cpuMs, gpuMs, texMB, throttle});
    elapsed_ += frameMs / 1000.0f;
    if (elapsed_ >= seconds_) active_ = false;
    return active_;
}

bool BenchRecorder::finish(const std::string& csvPath) const {
    if (samples_.empty()) return false;
    std::vector<float> ft;
    ft.reserve(samples_.size());
    double sum = 0;
    size_t peakMB = 0;
    int missed = 0;
    const float vsyncMs = 1000.0f / refreshHz_;
    for (const auto& s : samples_) {
        ft.push_back(s.frameMs);
        sum += s.frameMs;
        peakMB = std::max(peakMB, s.texMB);
        if (s.frameMs > vsyncMs * 1.5f) missed += static_cast<int>(s.frameMs / vsyncMs + 0.5f) - 1;
    }
    std::sort(ft.begin(), ft.end());
    auto pct = [&](float p) { return ft[std::min(ft.size() - 1, static_cast<size_t>(p * (ft.size() - 1)))]; };
    std::vector<float> gt;
    for (const auto& s : samples_) gt.push_back(s.gpuMs);
    std::sort(gt.begin(), gt.end());
    auto gpct = [&](float p) { return gt[std::min(gt.size() - 1, static_cast<size_t>(p * (gt.size() - 1)))]; };
    const float avg = static_cast<float>(sum / ft.size());
    const float p95 = pct(0.95f), p99 = pct(0.99f);
    // Pass: p99 within 1.25 frame periods of the target rate. A dropped frame (one extra vblank)
    // is >= 1.5 periods, so this catches stutter while tolerating CPU-side timing jitter.
    const bool pass = p99 <= 1.25f * vsyncMs;
    std::printf("BENCH mode=%s frames=%zu avg=%.2fms fps=%.1f p95=%.2fms p99=%.2fms max=%.2fms missed_vsyncs=%d "
                "gpu_p50=%.2fms gpu_p99=%.2fms gpu_max=%.2fms peak_tex=%zuMB throttle=%s result=%s\n",
                mode_.c_str(), ft.size(), avg, 1000.0f / avg, p95, p99, ft.back(), missed, gpct(0.5f), gpct(0.99f),
                gt.back(), peakMB, samples_.back().throttle.c_str(), pass ? "PASS" : "FAIL");
    if (!csvPath.empty()) {
        std::ofstream f(csvPath);
        f << "frame,frame_ms,cpu_ms,gpu_ms,texture_mb,throttled\n";
        for (size_t i = 0; i < samples_.size(); ++i)
            f << i << ',' << samples_[i].frameMs << ',' << samples_[i].cpuMs << ',' << samples_[i].gpuMs << ','
              << samples_[i].texMB << ',' << samples_[i].throttle << '\n';
    }
    return pass;
}

bool AutoQuality::update(float dt, float p95Ms, float budgetMs) {
    if (p95Ms > budgetMs * 0.9f) {
        overFor_ += dt;
        underFor_ = 0;
    } else if (p95Ms < budgetMs * 0.5f) {
        underFor_ += dt;
        overFor_ = 0;
    } else {
        overFor_ = underFor_ = 0;
    }
    if (overFor_ > 5.0f && level_ < kMaxLevel) {
        ++level_;
        overFor_ = 0;
        return true;
    }
    if (underFor_ > 20.0f && level_ > 0) {  // recover slowly to avoid oscillating
        --level_;
        underFor_ = 0;
        return true;
    }
    return false;
}

}  // namespace it
