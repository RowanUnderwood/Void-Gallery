#include "assets/ImageSource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>

#include "assets/HttpClient.h"

namespace fs = std::filesystem;

namespace it {

namespace {
// Paths are UTF-8 std::strings everywhere; on Windows they must reach the OS as wide strings.
fs::path toPath(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
std::string fromPath(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
}  // namespace

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(toPath(path), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    if (size < 0) return false;
    out.resize(static_cast<size_t>(size));
    f.seekg(0);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), size)) || size == 0;
}

bool writeFileAtomic(const std::string& path, const void* data, size_t size) {
    std::error_code ec;
    const fs::path p = toPath(path);
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

std::vector<std::string> listImageFiles(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(toPath(dir), ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = fromPath(e.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".webp" || ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            files.push_back(fromPath(fs::absolute(e.path())));
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool isAbsolutePath(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return p.size() > 2 && p[1] == ':' && (p[2] == '/' || p[2] == '\\');  // C:\... (on any platform)
}

namespace {

class FileSource final : public ImageSource {
public:
    explicit FileSource(std::string root) : root_(std::move(root)) {
        if (!root_.empty() && root_.back() != '/' && root_.back() != '\\') root_ += '/';
    }
    bool fetch(const std::string& path, std::vector<uint8_t>& out, std::string* err, bool) override {
        const std::string full = isAbsolutePath(path) ? path : root_ + path;
        if (readFile(full, out)) return true;
        if (err) *err = "cannot read " + full;
        return false;
    }
    std::string describe() const override { return "file:" + root_; }

private:
    std::string root_;
};

std::string urlEncodePath(const std::string& p) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : p) {
        if (isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

class HttpSource final : public ImageSource {
public:
    HttpSource(std::string base, std::string cacheDir) : base_(std::move(base)) {
        if (base_.back() != '/') base_ += '/';
        std::string host = base_.substr(base_.find("://") + 3);
        for (char& c : host) if (c == '/' || c == ':') c = '_';
        while (!host.empty() && host.back() == '_') host.pop_back();
        cacheRoot_ = cacheDir + "/" + host + "/";
    }

    bool fetch(const std::string& path, std::vector<uint8_t>& out, std::string* err,
               bool revalidate) override {
        if (isAbsolutePath(path)) return readFile(path, out);

        const std::string cachePath = cacheRoot_ + path;
        const std::string etagPath = cachePath + ".etag";
        std::error_code ec;
        const bool cached = fs::exists(toPath(cachePath), ec);

        if (cached && !revalidate && wasValidated(path)) return readFile(cachePath, out);
        if (offlineNow()) {
            if (cached && readFile(cachePath, out)) return true;
            if (err) *err = "offline and not cached: " + path;
            return false;
        }

        std::string oldEtag;
        if (cached) {
            std::vector<uint8_t> e;
            if (readFile(etagPath, e)) oldEtag.assign(e.begin(), e.end());
        }
        const std::string url = base_ + urlEncodePath(path);
        HttpResponse r = httpGet(url, oldEtag);

        if (!r.transportOk) {
            markOffline();
            if (cached && readFile(cachePath, out)) return true;
            if (err) *err = r.error;
            return false;
        }
        if (r.status == 304 && cached) {
            markValidated(path);
            return readFile(cachePath, out);
        }
        if (r.status == 200) {
            writeFileAtomic(cachePath, r.body.data(), r.body.size());
            if (!r.etag.empty()) writeFileAtomic(etagPath, r.etag.data(), r.etag.size());
            markValidated(path);
            out = std::move(r.body);
            return true;
        }
        if (err) *err = "http " + std::to_string(r.status) + " for " + url;
        return false;
    }

    std::string describe() const override { return base_; }
    bool isOffline() const override { return offlineNow(); }

private:
    using Clock = std::chrono::steady_clock;

    bool wasValidated(const std::string& p) {
        std::lock_guard<std::mutex> lk(mu_);
        return validated_.count(p) != 0;
    }
    void markValidated(const std::string& p) {
        std::lock_guard<std::mutex> lk(mu_);
        validated_.insert(p);
    }
    // After a connection failure, stay on the cache for a while instead of waiting for a
    // timeout on every image.
    void markOffline() {
        offlineUntil_ = Clock::now().time_since_epoch().count() +
                        std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(30)).count();
    }
    bool offlineNow() const { return Clock::now().time_since_epoch().count() < offlineUntil_.load(); }

    std::string base_;
    std::string cacheRoot_;
    std::mutex mu_;
    std::unordered_set<std::string> validated_;
    std::atomic<int64_t> offlineUntil_{0};
};

}  // namespace

std::shared_ptr<ImageSource> makeImageSource(const std::string& spec, const std::string& cacheDir) {
    if (spec.rfind("http://", 0) == 0 || spec.rfind("https://", 0) == 0)
        return std::make_shared<HttpSource>(spec, cacheDir);
    return std::make_shared<FileSource>(spec);
}

}  // namespace it
