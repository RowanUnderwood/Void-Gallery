#pragma once
// GPU frame time via GL_TIME_ELAPSED queries (desktop GL). Results are read a few frames later
// so the CPU never waits on the GPU. On GLES (Pi) timer queries are unavailable: always 0.

#include "render/GlApi.h"

namespace it {

class GpuTimer {
public:
    ~GpuTimer();
    void begin();        // call once per frame before rendering
    void end();          // after the last draw, before swap
    float lastMs() const { return lastMs_; }

private:
    static constexpr int kRing = 4;
    unsigned ids_[kRing] = {};
    bool pending_[kRing] = {};
    int cur_ = 0;
    bool init_ = false;
    bool active_ = false;
    float lastMs_ = 0.0f;
};

}  // namespace it
