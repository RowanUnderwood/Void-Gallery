#pragma once
// Fetch + decode on worker threads; completion callbacks run on the main thread in poll(),
// which is the only thread that touches GL.

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "assets/Decoder.h"
#include "assets/ImageSource.h"

namespace it {

class AsyncLoader {
public:
    using Done = std::function<void(bool ok, DecodedImage&& img, const std::string& error)>;

    explicit AsyncLoader(int threads);
    ~AsyncLoader();
    AsyncLoader(const AsyncLoader&) = delete;
    AsyncLoader& operator=(const AsyncLoader&) = delete;

    void load(std::shared_ptr<ImageSource> src, std::string path, int maxEdge, Done done);
    void poll();                 // main thread: run completed callbacks
    size_t queued() const;       // jobs not yet picked up by a worker

private:
    struct Job {
        std::shared_ptr<ImageSource> src;
        std::string path;
        int maxEdge;
        Done done;
    };
    struct Result {
        bool ok;
        DecodedImage img;
        std::string error;
        Done done;
    };

    void workerMain();

    mutable std::mutex jobMu_;
    std::condition_variable jobCv_;
    std::deque<Job> jobs_;
    bool stop_ = false;

    std::mutex resultMu_;
    std::deque<Result> results_;

    std::vector<std::thread> threads_;
};

}  // namespace it
