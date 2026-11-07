#include "miniadmin/ftpclient.hpp"

#include <algorithm>
#include <cctype>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <wininet.h>

namespace miniadmin {

namespace {

// the WinINet error code (and the FTP server's last reply) as text
std::string lastError() {
    const DWORD code = GetLastError();
    std::string msg = "(" + std::to_string(code) + ")";
    if (code == ERROR_INTERNET_EXTENDED_ERROR) {
        DWORD ext = 0, len = 0;
        InternetGetLastResponseInfoA(&ext, nullptr, &len);
        std::string buf(len + 1, '\0');
        if (len && InternetGetLastResponseInfoA(&ext, &buf[0], &len)) {
            buf.resize(len);
            while (!buf.empty() && (buf.back() == '\r' || buf.back() == '\n')) buf.pop_back();
            return buf;
        }
    }
    char* text = nullptr;
    HMODULE mod = GetModuleHandleA("wininet.dll");
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       mod, code, 0, (LPSTR)&text, 0, nullptr) && text) {
        std::string t = text;
        LocalFree(text);
        while (!t.empty() && (t.back() == '\r' || t.back() == '\n' || t.back() == ' ')) t.pop_back();
        msg = t + " " + msg;
    }
    return msg;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

FtpClient::FtpClient() = default;
FtpClient::~FtpClient() { close(); }

void FtpClient::close() {
    if (conn_) InternetCloseHandle((HINTERNET)conn_);
    if (inet_) InternetCloseHandle((HINTERNET)inet_);
    conn_ = inet_ = nullptr;
}

bool FtpClient::open(bool passive, std::string& err) {
    close();
    passive_ = passive;
    inet_ = InternetOpenA("miniAdmin3", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!inet_) { err = lastError(); return false; }
    DWORD to = 15000;  // gives up after this (the original waited forever)
    InternetSetOptionA((HINTERNET)inet_, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
    InternetSetOptionA((HINTERNET)inet_, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
    InternetSetOptionA((HINTERNET)inet_, INTERNET_OPTION_SEND_TIMEOUT, &to, sizeof to);
    InternetSetOptionA((HINTERNET)inet_, INTERNET_OPTION_DATA_RECEIVE_TIMEOUT, &to, sizeof to);
    InternetSetOptionA((HINTERNET)inet_, INTERNET_OPTION_DATA_SEND_TIMEOUT, &to, sizeof to);
    conn_ = InternetConnectA((HINTERNET)inet_, cfg_.host.c_str(), (INTERNET_PORT)cfg_.port,
                             cfg_.user.empty() ? nullptr : cfg_.user.c_str(),
                             cfg_.user.empty() ? nullptr : cfg_.password.c_str(), INTERNET_SERVICE_FTP,
                             passive ? INTERNET_FLAG_PASSIVE : 0, 0);
    if (!conn_) { err = lastError(); close(); return false; }
    return true;
}

bool FtpClient::connect(const FtpConfig& cfg, std::string& err) {
    cfg_ = cfg;
    return open(cfg.passive, err);
}

bool FtpClient::list(const std::string& dirIn, std::vector<FtpItem>& out, std::string& err) {
    const std::string dir = ftpNormalize(dirIn);
    for (int attempt = 0; attempt < 2; ++attempt) {
        out.clear();
        if (!conn_) return false;
        WIN32_FIND_DATAA fd{};
        const std::string pattern = dir == "/" ? "/*" : dir + "/*";
        HINTERNET f = FtpFindFirstFileA((HINTERNET)conn_, pattern.c_str(), &fd, INTERNET_FLAG_RELOAD, 0);
        if (!f) {
            const DWORD code = GetLastError();
            if (code == ERROR_NO_MORE_FILES) return true;  // empty folder
            err = lastError();
            // in active mode the data connection often cannot be made: retry passively
            if (attempt == 0 && !passive_ && code != ERROR_INTERNET_EXTENDED_ERROR) {
                std::string e2;
                if (open(true, e2)) continue;
            }
            return false;
        }
        do {
            const std::string n = fd.cFileName;
            if (n == "." || n == "..") continue;
            FtpItem it;
            it.name = n;
            it.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            it.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            out.push_back(it);
        } while (InternetFindNextFileA(f, &fd));
        InternetCloseHandle(f);
        std::sort(out.begin(), out.end(), [](const FtpItem& a, const FtpItem& b) {
            if (a.dir != b.dir) return a.dir;
            return lower(a.name) < lower(b.name);
        });
        return true;
    }
    return false;
}

bool FtpClient::download(const std::string& path, std::string& data, std::string& err) {
    data.clear();
    if (!conn_) return false;
    HINTERNET f = FtpOpenFileA((HINTERNET)conn_, ftpNormalize(path).c_str(), GENERIC_READ,
                               FTP_TRANSFER_TYPE_BINARY | INTERNET_FLAG_RELOAD, 0);
    if (!f) { err = lastError(); return false; }
    char buf[16384];
    DWORD n = 0;
    bool ok = true;
    for (;;) {
        if (!InternetReadFile(f, buf, sizeof buf, &n)) { err = lastError(); ok = false; break; }
        if (n == 0) break;
        data.append(buf, n);
        if (data.size() > 8u * 1024 * 1024) { err = "> 8 MB"; ok = false; break; }  // too big for a text editor
    }
    InternetCloseHandle(f);
    return ok;
}

bool FtpClient::upload(const std::string& path, const std::string& data, std::string& err) {
    if (!conn_) return false;
    HINTERNET f = FtpOpenFileA((HINTERNET)conn_, ftpNormalize(path).c_str(), GENERIC_WRITE, FTP_TRANSFER_TYPE_BINARY, 0);
    if (!f) { err = lastError(); return false; }
    size_t pos = 0;
    bool ok = true;
    while (pos < data.size()) {
        DWORD n = 0;
        const DWORD chunk = (DWORD)std::min<size_t>(16384, data.size() - pos);
        if (!InternetWriteFile(f, data.data() + pos, chunk, &n) || n == 0) { err = lastError(); ok = false; break; }
        pos += n;
    }
    if (!InternetCloseHandle(f) && ok) { err = lastError(); ok = false; }
    return ok;
}

}  // namespace miniadmin
