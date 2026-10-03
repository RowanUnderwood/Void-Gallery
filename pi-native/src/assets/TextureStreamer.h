#pragma once
// Port of index.html's TextureManager: master list -> shuffled play deck -> buffer of ready
// textures. Differences: decode happens on worker threads, GPU uploads are limited by bytes per
// frame, textures are shared by key (refcounted), and a texture is only taken from the buffer
// when a spawn task actually needs it.

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "assets/AsyncLoader.h"
#include "render/Gl.h"

namespace it {

struct GpuTexture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
    size_t bytes = 0;
    std::string key;
    float ratio() const { return height > 0 ? static_cast<float>(width) / height : 1.0f; }
    ~GpuTexture();
};
using TexRef = std::shared_ptr<GpuTexture>;

class TextureStreamer {
public:
    explicit TextureStreamer(AsyncLoader& loader);

    void setSequence(std::shared_ptr<ImageSource> src, std::vector<std::string> keys);
    void configure(size_t maxBuffer, size_t preloadTarget, int maxEdge);
    void setAnisotropy(int level);
    void clearBuffer();  // drop buffered textures (on-screen ones stay alive via their refs)

    // Main thread, once per frame: upload decoded images within budget and top up requests.
    void update(size_t byteBudget, int maxUploads);

    TexRef take();  // nullptr if nothing is ready yet

    size_t ready() const { return ready_.size(); }
    size_t inflight() const { return inflight_; }
    size_t decodedWaiting() const { return decoded_.size(); }
    size_t sequenceSize() const { return master_.size(); }
    size_t preloadTarget() const { return preload_; }
    bool preloaded() const { return preloaded_; }
    int maxEdge() const { return maxEdge_; }
    const std::string& lastError() const { return lastError_; }
    const std::string& lastTakenKey() const { return lastTaken_; }

    static size_t residentBytes();
    static size_t residentCount();

private:
    void refill();
    bool nextKey(std::string& key);
    void refillDeck();

    AsyncLoader& loader_;
    std::shared_ptr<ImageSource> src_;
    std::vector<std::string> master_;
    std::deque<std::string> deck_;
    std::deque<TexRef> ready_;
    struct Decoded {
        std::string key;
        DecodedImage img;
    };
    std::deque<Decoded> decoded_;
    std::unordered_map<std::string, std::weak_ptr<GpuTexture>> cache_;

    size_t inflight_ = 0;
    size_t maxBuffer_ = 24;
    size_t preload_ = 10;
    size_t takenSinceReset_ = 0;
    bool preloaded_ = false;
    int maxEdge_ = 1024;
    int aniso_ = 1;
    uint64_t generation_ = 0;
    int failStreak_ = 0;
    double retryAt_ = 0.0;
    std::string lastError_;
    std::string lastTaken_;
};

}  // namespace it
