#include "util/Thermal.h"

#include <chrono>
#include <cstdio>
#include <fstream>

namespace it {

ThermalMonitor::ThermalMonitor() : thread_([this] { run(); }) {}

ThermalMonitor::~ThermalMonitor() {
    stop_ = true;
    thread_.join();
}

std::string ThermalMonitor::throttled() const {
    std::lock_guard<std::mutex> lk(mu_);
    return throttled_;
}

void ThermalMonitor::run() {
#if defined(_WIN32)
    return;  // Pi-specific (sysfs + vcgencmd); the stats overlay shows "n/a" on Windows
#else
    using namespace std::chrono_literals;
    while (!stop_) {
        std::ifstream t("/sys/class/thermal/thermal_zone0/temp");
        long milli = 0;
        if (t >> milli) temp_ = milli / 1000.0f;

        std::string flags = "n/a";
        if (FILE* p = popen("vcgencmd get_throttled 2>/dev/null", "r")) {
            char buf[64] = {};
            if (fgets(buf, sizeof(buf), p)) {
                std::string s(buf);
                const auto eq = s.find('=');
                if (eq != std::string::npos) {
                    flags = s.substr(eq + 1);
                    while (!flags.empty() && (flags.back() == '\n' || flags.back() == '\r')) flags.pop_back();
                }
            }
            pclose(p);
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            throttled_ = flags;
        }
        for (int i = 0; i < 20 && !stop_; ++i) std::this_thread::sleep_for(100ms);
    }
#endif
}

}  // namespace it
