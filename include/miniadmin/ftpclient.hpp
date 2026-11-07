// FTP for the "Edit server files via FTP" window and the FTP test (WinINet instead of the original Indy TIdFTP).
// Every call BLOCKS: call only from a worker thread (the original ran it on the main thread, that is why it froze).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace miniadmin {

struct FtpItem {
    std::string name;
    bool dir = false;
    uint64_t size = 0;
};

// ---- paths (on FTP the separator is always '/')
// "/a/b/" → "/a/b", "" → "/", "a" → "/a"
inline std::string ftpNormalize(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    if (p.empty() || p[0] != '/') p = "/" + p;
    std::string o;
    for (char c : p) if (!(c == '/' && !o.empty() && o.back() == '/')) o += c;
    while (o.size() > 1 && o.back() == '/') o.pop_back();
    return o;
}
inline std::string ftpJoin(const std::string& dir, const std::string& name) {
    const std::string d = ftpNormalize(dir);
    return ftpNormalize(d == "/" ? "/" + name : d + "/" + name);
}
inline std::string ftpParent(const std::string& path) {
    const std::string p = ftpNormalize(path);
    const size_t s = p.find_last_of('/');
    return s == 0 || s == std::string::npos ? std::string("/") : p.substr(0, s);
}

struct FtpConfig {
    std::string host;
    int port = 21;
    std::string user, password;
    bool passive = false;
};

class FtpClient {
public:
    FtpClient();
    ~FtpClient();
    FtpClient(const FtpClient&) = delete;
    FtpClient& operator=(const FtpClient&) = delete;

    // Connect and log in. If listing does not work in active mode (common behind NAT / a firewall, and the
    // original froze in this case), list() switches to passive mode automatically.
    bool connect(const FtpConfig& cfg, std::string& err);
    bool list(const std::string& dir, std::vector<FtpItem>& out, std::string& err);  // folders first, by name
    bool download(const std::string& path, std::string& data, std::string& err);
    bool upload(const std::string& path, const std::string& data, std::string& err);
    bool usedPassive() const { return passive_; }
    void close();

private:
    bool open(bool passive, std::string& err);
    FtpConfig cfg_;
    bool passive_ = false;
    void* inet_ = nullptr;
    void* conn_ = nullptr;
};

}  // namespace miniadmin
