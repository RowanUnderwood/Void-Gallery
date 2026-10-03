#pragma once
// Desktop frame pacing for the 30 / 60 / 120 fps caps and "unlocked".
//   - cap divides the display refresh (60 on 120 Hz, 30 on 60 Hz, ...): vsync with swap interval
//     refresh/cap, so every frame lands on a vblank with no timer involved;
//   - otherwise (e.g. 60 on a 144 Hz VRR monitor): vsync plus a precise software limiter, which is
//     what G-Sync/FreeSync want (cap below the refresh rate);
//   - unlocked (cap 0): swap interval 0, no limiter; VRR or tearing, depending on the display.

#include <cstdint>
#include <string>

namespace it {

class FramePacer {
public:
    // Returns the swap interval to request.
    int configure(int capHz, double refreshHz);
    void beforeSwap();   // sleeps until the next frame slot when software limiting
    const std::string& description() const { return desc_; }
    double targetHz() const { return targetHz_; }

private:
    bool limit_ = false;
    double period_ = 0.0;     // seconds
    double targetHz_ = 0.0;
    uint64_t next_ = 0;       // performance-counter tick of the next frame slot
    std::string desc_;
};

}  // namespace it
