#pragma once
// Minimal blocking HTTP GET with conditional requests. libcurl on Linux, WinHTTP on Windows.
// Safe to call from several threads at once (each thread keeps its own connection state).

#include <cstdint>
#include <string>
#include <vector>

namespace it {

struct HttpResponse {
    bool transportOk = false;   // false: could not reach the server (DNS, refused, timeout)
    long status = 0;            // HTTP status when transportOk
    std::vector<uint8_t> body;
    std::string etag;
    std::string error;
};

HttpResponse httpGet(const std::string& url, const std::string& ifNoneMatch);

void httpGlobalInit();
void httpGlobalCleanup();

}  // namespace it
