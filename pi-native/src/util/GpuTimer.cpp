#include "util/GpuTimer.h"

namespace it {

#if defined(IT_DESKTOP_GL)
GpuTimer::~GpuTimer() {
    if (init_) glDeleteQueries(kRing, ids_);
}

void GpuTimer::begin() {
    if (!init_) {
        glGenQueries(kRing, ids_);
        init_ = true;
    }
    // Collect the oldest finished result first (its slot is reused below).
    const int slot = cur_;
    if (pending_[slot]) {
        GLint ready = 0;
        glGetQueryObjectiv(ids_[slot], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) {  // GPU more than kRing frames behind: skip measuring this frame
            active_ = false;
            return;
        }
        GLuint64 ns = 0;
        glGetQueryObjectui64v(ids_[slot], GL_QUERY_RESULT, &ns);
        lastMs_ = static_cast<float>(ns / 1.0e6);
        pending_[slot] = false;
    }
    glBeginQuery(GL_TIME_ELAPSED, ids_[slot]);
    active_ = true;
}

void GpuTimer::end() {
    if (!active_) return;
    glEndQuery(GL_TIME_ELAPSED);
    pending_[cur_] = true;
    cur_ = (cur_ + 1) % kRing;
    active_ = false;
}
#else
GpuTimer::~GpuTimer() {}
void GpuTimer::begin() {}
void GpuTimer::end() {}
#endif

}  // namespace it
