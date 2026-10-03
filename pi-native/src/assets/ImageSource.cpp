#include "assets/ImageSource.h"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>

namespace fs = std::filesystem;

namespace it {

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    if (size < 0) return false;
    out.resize(static_cast<size_t>(size));
    f.seekg(0);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), size)) || size == 0;
}

bool writeFileAtomic(const std::string& path, const void* data, size_t size) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!f) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

std::vector<std::string> listImageFiles(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".webp" || ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            files.push_back(fs::absolute(e.path()).string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

namespace {

bool isAbsolute(const std::string& p) { return !p.empty() && p[0] == '/'; }

class FileSource final : public ImageSource {
public:
    explicit FileSource(std::string root) : root_(std::move(root)) {
        if (!root_.empty() && root_.back() != '/') root_ += '/';
    }
    bool fetch(const std::string& path, std::vector<uint8_t>& out, std::string* err, bool) override {
        const std::string full = isAbsolute(path) ? path : root_ + path;
        if (readFile(full, out)) return true;
        if (err) *err = "cannot read " + full;
        return false;
    }
    std::string describe() const override { return "file:" + root_; }

private:
    std::string root_;
};

size_t curlWrite(char* ptr, size_t size, size_t n, void* user) {
    auto* buf = static_cast<std::vector<uint8_t>*>(user);
    buf->insert(buf->end(), ptr, ptr + size * n);
    return size * n;
}

size_t curlHeader(char* ptr, size_t size, size_t n, void* user) {
    auto* etag = static_cast<std::string*>(user);
    std::string line(ptr, size * n);
    if (line.size() > 5) {
        std::string key = line.substr(0, 5);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key == "etag:") {
            std::string v = line.substr(5);
            v.erase(0, v.find_first_not_of(" \t"));
            v.erase(v.find_last_not_of(" \t\r\n") + 1);
            *etag = v;
        }
    }
    return size * n;
}

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

// One CURL easy handle per worker thread so connections are kept alive between requests.
struct CurlHandle {
    CURL* h = curl_easy_init();
    ~CurlHandle() { if (h) curl_easy_cleanup(h); }
};

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
        if (isAbsolute(path)) return readFile(path, out);

        const std::string cachePath = cacheRoot_ + path;
        const std::string etagPath = cachePath + ".etag";
        const bool cached = fs::exists(cachePath);

        if (cached && !revalidate && wasValidated(path)) return readFile(cachePath, out);
        if (offlineNow()) {
            if (cached && readFile(cachePath, out)) return true;
            if (err) *err = "offline and not cached: " + path;
            return false;
        }

        thread_local CurlHandle curl;
        if (!curl.h) {
            if (err) *err = "curl init failed";
            return false;
        }
        std::vector<uint8_t> body;
        std::string etag;
        std::string oldEtag;
        if (cached) {
            std::vector<uint8_t> e;
            if (readFile(etagPath, e)) oldEtag.assign(e.begin(), e.end());
        }
        const std::string url = base_ + urlEncodePath(path);
        curl_slist* headers = nullptr;
        if (!oldEtag.empty()) headers = curl_slist_append(headers, ("If-None-Match: " + oldEtag).c_str());

        curl_easy_reset(curl.h);
        curl_easy_setopt(curl.h, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.h, CURLOPT_WRITEFUNCTION, curlWrite);
        curl_easy_setopt(curl.h, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl.h, CURLOPT_HEADERFUNCTION, curlHeader);
        curl_easy_setopt(curl.h, CURLOPT_HEADERDATA, &etag);
        curl_easy_setopt(curl.h, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl.h, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(curl.h, CURLOPT_TIMEOUT_MS, 30000L);
        curl_easy_setopt(curl.h, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.h, CURLOPT_NOSIGNAL, 1L);
        const CURLcode rc = curl_easy_perform(curl.h);
        long status = 0;
        curl_easy_getinfo(curl.h, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);

        if (rc != CURLE_OK) {
            markOffline();
            if (cached && readFile(cachePath, out)) return true;
            if (err) *err = std::string("http error: ") + curl_easy_strerror(rc) + " (" + url + ")";
            return false;
        }
        if (status == 304 && cached) {
            markValidated(path);
            return readFile(cachePath, out);
        }
        if (status == 200) {
            writeFileAtomic(cachePath, body.data(), body.size());
            if (!etag.empty()) writeFileAtomic(etagPath, etag.data(), etag.size());
            markValidated(path);
            out = std::move(body);
            return true;
        }
        if (err) *err = "http " + std::to_string(status) + " for " + url;
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
