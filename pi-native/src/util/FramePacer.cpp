#include "util/FramePacer.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace it {

int FramePacer::configure(int capHz, double refreshHz) {
    limit_ = false;
    next_ = 0;
    if (refreshHz <= 0) refreshHz = 60.0;
    char buf[128];
    if (capHz <= 0) {
        targetHz_ = 0.0;
        desc_ = "unlocked (vsync off)";
        return 0;
    }
    const int n = std::max(1, static_cast<int>(std::lround(refreshHz / capHz)));
    const double viaVsync = refreshHz / n;
    if (capHz >= refreshHz * 0.97 || std::abs(viaVsync - capHz) <= capHz * 0.03) {
        targetHz_ = std::min<double>(capHz, viaVsync);
        std::snprintf(buf, sizeof(buf), "%.0f fps (vsync, interval %d @ %.0f Hz)", targetHz_, n, refreshHz);
        desc_ = buf;
        return capHz >= refreshHz * 0.97 ? 1 : n;
    }
    limit_ = true;
    targetHz_ = capHz;
    period_ = 1.0 / capHz;
    std::snprintf(buf, sizeof(buf), "%d fps (vsync + limiter @ %.0f Hz; best with VRR)", capHz, refreshHz);
    desc_ = buf;
    return 1;
}

void FramePacer::beforeSwap() {
    if (!limit_) return;
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t periodTicks = static_cast<uint64_t>(period_ * static_cast<double>(freq));
    uint64_t now = SDL_GetPerformanceCounter();
    if (next_ == 0 || now > next_ + periodTicks) next_ = now;  // first frame, or we fell far behind
    // Coarse sleep until ~1.5 ms before the slot, then spin (Sleep granularity is ~1 ms).
    const uint64_t spinTicks = freq * 15 / 10000;
    while (now + spinTicks < next_) {
        SDL_Delay(1);
        now = SDL_GetPerformanceCounter();
    }
    while (now < next_) now = SDL_GetPerformanceCounter();
    next_ += periodTicks;
}

}  // namespace it
