#include "assets/TextureStreamer.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

#include "render/MathUtil.h"

namespace it {

namespace {
std::atomic<size_t> gResidentBytes{0};
std::atomic<size_t> gResidentCount{0};
constexpr size_t kMaxInflight = 8;

double nowSeconds() { return SDL_GetTicks64() / 1000.0; }
}  // namespace

GpuTexture::~GpuTexture() {
    if (id) {
        glDeleteTextures(1, &id);
        gResidentBytes -= bytes;
        --gResidentCount;
    }
}

size_t TextureStreamer::residentBytes() { return gResidentBytes; }
size_t TextureStreamer::residentCount() { return gResidentCount; }

TextureStreamer::TextureStreamer(AsyncLoader& loader) : loader_(loader) {}

void TextureStreamer::setSequence(std::shared_ptr<ImageSource> src, std::vector<std::string> keys) {
    src_ = std::move(src);
    master_ = std::move(keys);
    deck_.clear();
    clearBuffer();
}

void TextureStreamer::configure(size_t maxBuffer, size_t preloadTarget, int maxEdge) {
    if (maxEdge != maxEdge_) {
        maxEdge_ = maxEdge;
        clearBuffer();  // buffered textures were decoded at the old size
        cache_.clear();
    }
    maxBuffer_ = std::max<size_t>(1, maxBuffer);
    preload_ = std::max<size_t>(1, preloadTarget);
    takenSinceReset_ = 0;
    preloaded_ = ready_.size() >= preload_;
}

void TextureStreamer::setAnisotropy(int level) {
    aniso_ = level;
    for (auto& t : ready_) gl::setAnisotropy(t->id, level);
    for (auto& [key, weak] : cache_)
        if (auto t = weak.lock()) gl::setAnisotropy(t->id, level);
}

void TextureStreamer::clearBuffer() {
    ++generation_;  // results of in-flight requests are discarded when they arrive
    inflight_ = 0;
    ready_.clear();
    decoded_.clear();
    takenSinceReset_ = 0;
    preloaded_ = false;
    failStreak_ = 0;
    retryAt_ = 0.0;
}

void TextureStreamer::refillDeck() {
    if (master_.empty()) return;
    // Same as refillDeck() in index.html: repeat the master list if it is smaller than the buffer.
    const size_t target = std::max(master_.size(), maxBuffer_);
    std::vector<std::string> deck;
    deck.reserve(target);
    while (deck.size() < target) deck.insert(deck.end(), master_.begin(), master_.end());
    std::shuffle(deck.begin(), deck.end(), rng());
    deck_.assign(deck.begin(), deck.end());
}

bool TextureStreamer::nextKey(std::string& key) {
    if (deck_.empty()) refillDeck();
    if (deck_.empty()) return false;
    key = std::move(deck_.front());
    deck_.pop_front();
    return true;
}

void TextureStreamer::refill() {
    if (!src_ || master_.empty()) return;
    if (failStreak_ > 20 && nowSeconds() < retryAt_) return;  // source unavailable: back off
    while (ready_.size() + decoded_.size() + inflight_ < maxBuffer_ && inflight_ < kMaxInflight) {
        std::string key;
        if (!nextKey(key)) return;
        if (auto it = cache_.find(key); it != cache_.end()) {
            if (auto shared = it->second.lock()) {  // already resident: reuse, no decode
                ready_.push_back(std::move(shared));
                continue;
            }
            cache_.erase(it);
        }
        ++inflight_;
        const uint64_t gen = generation_;
        loader_.load(src_, key, maxEdge_, [this, gen, key](bool ok, DecodedImage&& img, const std::string& err) {
            if (gen != generation_) return;
            --inflight_;
            if (!ok) {
                lastError_ = err;
                if (++failStreak_ > 20) retryAt_ = nowSeconds() + 2.0;
                std::fprintf(stderr, "[textures] %s\n", err.c_str());
                return;
            }
            failStreak_ = 0;
            decoded_.push_back(Decoded{key, std::move(img)});
        });
    }
}

void TextureStreamer::update(size_t byteBudget, int maxUploads) {
    size_t bytes = 0;
    int uploads = 0;
    while (!decoded_.empty() && uploads < maxUploads) {
        Decoded& d = decoded_.front();
        if (uploads > 0 && bytes + d.img.bytes() > byteBudget) break;
        auto tex = std::make_shared<GpuTexture>();
        tex->id = gl::createTexture(d.img, gl::TextureOptions{true, true, false, aniso_});
        tex->width = d.img.width;
        tex->height = d.img.height;
        tex->hasAlpha = d.img.hasAlpha;
        tex->opaque[0] = d.img.opaqueU0;
        tex->opaque[1] = d.img.opaqueV0;
        tex->opaque[2] = d.img.opaqueU1;
        tex->opaque[3] = d.img.opaqueV1;
        tex->bytes = d.img.bytes() * 4 / 3;  // + mip chain
        tex->key = d.key;
        gResidentBytes += tex->bytes;
        ++gResidentCount;
        bytes += d.img.bytes();
        ++uploads;
        cache_[d.key] = tex;
        ready_.push_back(std::move(tex));
        decoded_.pop_front();
    }
    if (!preloaded_ && ready_.size() + takenSinceReset_ >= std::min(preload_, std::max<size_t>(1, master_.size())))
        preloaded_ = true;
    refill();
    if (cache_.size() > 4096) {  // prune dead weak refs occasionally
        for (auto it = cache_.begin(); it != cache_.end();)
            it = it->second.expired() ? cache_.erase(it) : std::next(it);
    }
}

TexRef TextureStreamer::take() {
    if (ready_.empty()) return nullptr;
    TexRef t = std::move(ready_.front());
    ready_.pop_front();
    ++takenSinceReset_;
    lastTaken_ = t->key;
    return t;
}

}  // namespace it
