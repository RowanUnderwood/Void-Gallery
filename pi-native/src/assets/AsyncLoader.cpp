#include "assets/AsyncLoader.h"

#include <algorithm>

namespace it {

AsyncLoader::AsyncLoader(int threads) {
    for (int i = 0; i < std::max(1, threads); ++i) threads_.emplace_back([this] { workerMain(); });
}

AsyncLoader::~AsyncLoader() {
    {
        std::lock_guard<std::mutex> lk(jobMu_);
        stop_ = true;
        jobs_.clear();
    }
    jobCv_.notify_all();
    for (auto& t : threads_) t.join();
}

void AsyncLoader::load(std::shared_ptr<ImageSource> src, std::string path, int maxEdge, Done done) {
    {
        std::lock_guard<std::mutex> lk(jobMu_);
        jobs_.push_back(Job{std::move(src), std::move(path), maxEdge, std::move(done)});
    }
    jobCv_.notify_one();
}

size_t AsyncLoader::queued() const {
    std::lock_guard<std::mutex> lk(jobMu_);
    return jobs_.size();
}

void AsyncLoader::poll() {
    std::deque<Result> ready;
    {
        std::lock_guard<std::mutex> lk(resultMu_);
        ready.swap(results_);
    }
    for (auto& r : ready) r.done(r.ok, std::move(r.img), r.error);
}

void AsyncLoader::workerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(jobMu_);
            jobCv_.wait(lk, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        Result r{false, {}, {}, std::move(job.done)};
        std::vector<uint8_t> bytes;
        if (job.src->fetch(job.path, bytes, &r.error)) {
            r.ok = decodeImage(bytes.data(), bytes.size(), job.maxEdge, r.img, &r.error);
            if (!r.ok) r.error = job.path + ": " + r.error;
        }
        std::lock_guard<std::mutex> lk(resultMu_);
        results_.push_back(std::move(r));
    }
}

}  // namespace it
