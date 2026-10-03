#pragma once
// Where image/config bytes come from: the nginx server over HTTP (with an on-disk cache for
// offline use) or a local directory laid out like the web root.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace it {

class ImageSource {
public:
    virtual ~ImageSource() = default;

    // `path` is relative to the source root (e.g. "images/quarterres/12.webp").
    // Absolute filesystem paths ("/media/usb/a.png") are always read from local disk.
    // `revalidate` forces a server round-trip even if the file was validated this session
    // (used for small, frequently-changing files such as config.json).
    virtual bool fetch(const std::string& path, std::vector<uint8_t>& out, std::string* err = nullptr,
                       bool revalidate = false) = 0;

    virtual std::string describe() const = 0;
    virtual bool isOffline() const { return false; }
};

// `spec` is either an http(s):// base URL or a local directory.
std::shared_ptr<ImageSource> makeImageSource(const std::string& spec, const std::string& cacheDir);

bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFileAtomic(const std::string& path, const void* data, size_t size);
std::vector<std::string> listImageFiles(const std::string& dir);  // sorted absolute paths
bool isAbsolutePath(const std::string& p);                        // "/x", "\\x" or "C:\x"

}  // namespace it
