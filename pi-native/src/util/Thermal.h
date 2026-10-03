#pragma once
// Background poll of SoC temperature and the firmware throttle flags (vcgencmd get_throttled).

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace it {

class ThermalMonitor {
public:
    ThermalMonitor();
    ~ThermalMonitor();
    float temperatureC() const { return temp_.load(); }  // < 0 if unavailable
    std::string throttled() const;                        // e.g. "0x0", "n/a"

private:
    void run();
    std::atomic<float> temp_{-1.0f};
    mutable std::mutex mu_;
    std::string throttled_ = "n/a";
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace it
