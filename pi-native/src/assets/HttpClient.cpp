#include "assets/HttpClient.h"

#include <algorithm>
#include <cctype>

#if defined(IT_WINDOWS)
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace it {

#if defined(IT_WINDOWS)
// ----------------------------------------------------------------------------- WinHTTP
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x = nullptr) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

// One session per thread; WinHTTP pools keep-alive connections per session.
HINTERNET session() {
    thread_local Handle s(WinHttpOpen(L"ImageTunnel/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (s.h) WinHttpSetTimeouts(s.h, 3000, 3000, 30000, 30000);
    return s.h;
}

std::string lastError(const char* what) { return std::string(what) + " failed (" + std::to_string(GetLastError()) + ")"; }

}  // namespace

void httpGlobalInit() {}
void httpGlobalCleanup() {}

HttpResponse httpGet(const std::string& url, const std::string& ifNoneMatch) {
    HttpResponse r;
    HINTERNET ses = session();
    if (!ses) {
        r.error = lastError("WinHttpOpen");
        return r;
    }
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "bad url " + url;
        return r;
    }
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    Handle con(WinHttpConnect(ses, host.c_str(), uc.nPort, 0));
    if (!con.h) {
        r.error = lastError("WinHttpConnect");
        return r;
    }
    Handle req(WinHttpOpenRequest(con.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    if (!req.h) {
        r.error = lastError("WinHttpOpenRequest");
        return r;
    }
    std::wstring headers;
    if (!ifNoneMatch.empty()) headers = L"If-None-Match: " + widen(ifNoneMatch) + L"\r\n";
    if (!WinHttpSendRequest(req.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        r.error = lastError("HTTP request") + " (" + url + ")";
        return r;
    }
    r.transportOk = true;
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<long>(status);
    wchar_t etag[256];
    DWORD etagLen = sizeof(etag);
    if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_ETAG, WINHTTP_HEADER_NAME_BY_INDEX, etag, &etagLen,
                            WINHTTP_NO_HEADER_INDEX))
        r.etag = narrow(std::wstring(etag, etagLen / sizeof(wchar_t)));
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        const size_t off = r.body.size();
        r.body.resize(off + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, r.body.data() + off, avail, &got)) {
            r.transportOk = false;
            r.error = lastError("WinHttpReadData");
            return r;
        }
        r.body.resize(off + got);
    }
    return r;
}

#else
// ----------------------------------------------------------------------------- libcurl
namespace {

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

// One CURL easy handle per worker thread so connections are kept alive between requests.
struct CurlHandle {
    CURL* h = curl_easy_init();
    ~CurlHandle() { if (h) curl_easy_cleanup(h); }
};

}  // namespace

void httpGlobalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void httpGlobalCleanup() { curl_global_cleanup(); }

HttpResponse httpGet(const std::string& url, const std::string& ifNoneMatch) {
    HttpResponse r;
    thread_local CurlHandle curl;
    if (!curl.h) {
        r.error = "curl init failed";
        return r;
    }
    curl_slist* headers = nullptr;
    if (!ifNoneMatch.empty()) headers = curl_slist_append(headers, ("If-None-Match: " + ifNoneMatch).c_str());
    curl_easy_reset(curl.h);
    curl_easy_setopt(curl.h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.h, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl.h, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(curl.h, CURLOPT_HEADERFUNCTION, curlHeader);
    curl_easy_setopt(curl.h, CURLOPT_HEADERDATA, &r.etag);
    curl_easy_setopt(curl.h, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl.h, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    curl_easy_setopt(curl.h, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(curl.h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.h, CURLOPT_NOSIGNAL, 1L);
    const CURLcode rc = curl_easy_perform(curl.h);
    curl_easy_getinfo(curl.h, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(headers);
    if (rc != CURLE_OK) {
        r.error = std::string("http error: ") + curl_easy_strerror(rc) + " (" + url + ")";
        return r;
    }
    r.transportOk = true;
    return r;
}
#endif

}  // namespace it
