#include "miniadmin/master.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>

#include "miniadmin/status.hpp"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using msock_t = SOCKET;
#  define MCLOSESOCK closesocket
static const msock_t kMBadSock = INVALID_SOCKET;
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using msock_t = int;
#  define MCLOSESOCK close
static const msock_t kMBadSock = -1;
#endif

namespace miniadmin {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
int toInt(const std::string& s, int def) {
    if (s.empty()) return def;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    return (end && end != s.c_str()) ? (int)v : def;
}
std::string addrKey(const Endpoint& ep) { return ep.host + ":" + std::to_string(ep.port); }

// Unconnected UDP socket: sends to many addresses, receives from all of them.
class UdpMulti {
public:
    UdpMulti() {
        s_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s_ != kMBadSock) {
            int big = 1 << 20;  // many replies arriving at once
            setsockopt(s_, SOL_SOCKET, SO_RCVBUF, (const char*)&big, sizeof big);
#ifdef _WIN32
            // the ICMP "port unreachable" message of non-answering addresses must not interrupt receiving
            const DWORD kSioUdpConnReset = 0x9800000C;  // SIO_UDP_CONNRESET
            BOOL off = FALSE;
            DWORD ret = 0;
            WSAIoctl(s_, kSioUdpConnReset, &off, sizeof off, nullptr, 0, &ret, nullptr, nullptr);
#endif
        }
    }
    ~UdpMulti() { if (s_ != kMBadSock) MCLOSESOCK(s_); }
    bool ok() const { return s_ != kMBadSock; }
    static bool resolve(const Endpoint& ep, sockaddr_in& out) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(ep.host.c_str(), std::to_string(ep.port).c_str(), &hints, &res) != 0 || !res) return false;
        out = *(const sockaddr_in*)res->ai_addr;
        freeaddrinfo(res);
        return true;
    }
    bool sendTo(const sockaddr_in& a, const std::string& data) {
        return ::sendto(s_, data.data(), (int)data.size(), 0, (const sockaddr*)&a, sizeof a) == (int)data.size();
    }
    // one datagram; false if nothing came within timeoutMs
    bool recvFrom(std::string& data, Endpoint& from, int timeoutMs) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(s_, &fds);
        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        if (::select((int)s_ + 1, &fds, nullptr, nullptr, &tv) <= 0) return false;
        char buf[16384];
        sockaddr_in a{};
#ifdef _WIN32
        int len = sizeof a;
#else
        socklen_t len = sizeof a;
#endif
        const int n = (int)::recvfrom(s_, buf, sizeof buf, 0, (sockaddr*)&a, &len);
        if (n <= 0) return false;  // (on Windows the ICMP "port unreachable" also ends up here)
        data.assign(buf, (size_t)n);
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        from.host = ip;
        from.port = ntohs(a.sin_port);
        return true;
    }

private:
    msock_t s_ = kMBadSock;
};

}  // namespace

const std::vector<MasterVersion>& masterVersions() {
    static const std::vector<MasterVersion> v = {
        {"COD4 1.7", "c46", Game::COD4, 6, 20810},    {"COD4 1.6", "c45", Game::COD4, 5, 20810},
        {"COD4 1.5", "c44", Game::COD4, 4, 20810},    {"COD4 1.4", "c43", Game::COD4, 3, 20810},
        {"COD4 1.3", "c42", Game::COD4, 2, 20810},    {"COD2 1.3", "118", Game::COD2, 118, 20710},
        {"COD2 1.2", "117", Game::COD2, 117, 20710},  {"COD2 1.0", "115", Game::COD2, 115, 20710},
        {"COD:UO 1.51", "22", Game::CODUO, 22, 20610}, {"COD1 1.5", "6", Game::COD1, 6, 20510},
    };
    return v;
}

std::vector<MasterEntry> parseMasterList(const std::string& text) {
    std::vector<MasterEntry> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        std::vector<std::string> parts;
        size_t a = 0;
        for (;;) {
            const size_t c = line.find(':', a);
            parts.push_back(trim(line.substr(a, c == std::string::npos ? std::string::npos : c - a)));
            if (c == std::string::npos) break;
            a = c + 1;
        }
        MasterEntry m;
        auto isPort = [](const std::string& s) {
            return !s.empty() && s.size() <= 5 && std::all_of(s.begin(), s.end(), ::isdigit) && std::atoi(s.c_str()) > 0 &&
                   std::atoi(s.c_str()) < 65536;
        };
        if (parts.size() == 3 && isPort(parts[2])) {
            m.key = lower(parts[0]);
            m.ep.host = parts[1];
            m.ep.port = (uint16_t)std::atoi(parts[2].c_str());
            m.hasPort = true;
        } else if (parts.size() == 2 && isPort(parts[1])) {
            m.ep.host = parts[0];
            m.ep.port = (uint16_t)std::atoi(parts[1].c_str());
            m.hasPort = true;
        } else if (parts.size() == 1) {
            m.ep.host = parts[0];
        } else {
            continue;
        }
        if (m.ep.host.empty()) continue;
        out.push_back(m);
    }
    return out;
}

std::vector<Endpoint> mastersFor(const std::vector<MasterEntry>& list, const MasterVersion& v) {
    std::vector<Endpoint> out;
    auto add = [&](Endpoint ep) {
        for (const auto& o : out)
            if (lower(o.host) == lower(ep.host) && o.port == ep.port) return;
        out.push_back(ep);
    };
    for (const auto& m : list)
        if (m.key == v.key) add(m.ep);
    for (const auto& m : list)
        if (m.key.empty()) add(Endpoint{m.ep.host, m.hasPort ? m.ep.port : v.port});
    // the official master server by name (still answers in 2026; the old IPs do not)
    switch (v.game) {
        case Game::COD4: add(Endpoint{"cod4master.activision.com", v.port}); break;
        case Game::COD2: add(Endpoint{"cod2master.activision.com", v.port}); break;
        case Game::CODUO:
        case Game::COD1: add(Endpoint{"codmaster.activision.com", v.port}); break;
        default: break;
    }
    return out;
}

std::string getServersPacket(int protocol) { return makeOob("getservers " + std::to_string(protocol) + " empty full"); }

MasterEnd parseGetServersResponse(const std::string& p, std::vector<Endpoint>& out) {
    const std::string head = "getserversResponse";
    if (!hasOobPrefix(p)) return MasterEnd::None;
    size_t i = p.find(head);
    if (i == std::string::npos || i > 8) return MasterEnd::None;
    i += head.size();
    while (i < p.size() && p[i] != '\\') ++i;  // (followed by \n or nothing)
    MasterEnd end = MasterEnd::None;
    while (i < p.size()) {
        if (p[i] != '\\') { ++i; continue; }
        // end of a packet: \EOT (more to come), end of the list: \EOF (followed by zero bytes at most)
        if (i + 4 <= p.size() && (p.compare(i + 1, 3, "EOT") == 0 || p.compare(i + 1, 3, "EOF") == 0) &&
            p.find_first_not_of(std::string(1, '\0'), i + 4) == std::string::npos) {
            end = p.compare(i + 1, 3, "EOF") == 0 ? MasterEnd::List : MasterEnd::Packet;
            break;
        }
        if (i + 7 > p.size()) break;
        const unsigned char* b = (const unsigned char*)p.data() + i + 1;
        const uint16_t port = (uint16_t)((b[4] << 8) | b[5]);
        if (port != 0 && !(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0)) {
            Endpoint ep{std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." +
                            std::to_string(b[3]),
                        port};
            out.push_back(ep);
        }
        i += 7;
    }
    return end;
}

bool parseInfoResponse(const std::string& p, BrowserServer& s) {
    if (!hasOobPrefix(p)) return false;
    const size_t h = p.find("infoResponse");
    if (h == std::string::npos || h > 8) return false;
    size_t i = p.find('\\', h);
    if (i == std::string::npos) return false;
    std::map<std::string, std::string> kv;
    while (i < p.size() && p[i] == '\\') {
        const size_t k = p.find('\\', i + 1);
        if (k == std::string::npos) break;
        size_t v = p.find('\\', k + 1);
        if (v == std::string::npos) v = p.size();
        std::string val = p.substr(k + 1, v - k - 1);
        while (!val.empty() && (val.back() == '\n' || val.back() == '\0')) val.pop_back();
        kv[lower(p.substr(i + 1, k - i - 1))] = val;
        i = v;
    }
    auto get = [&](const char* key) -> const std::string* {
        auto it = kv.find(key);
        return it == kv.end() ? nullptr : &it->second;
    };
    auto num = [&](const char* key, int def) { const std::string* v = get(key); return v ? toInt(*v, def) : def; };
    if (const std::string* v = get("hostname")) s.hostname = *v;
    if (const std::string* v = get("mapname")) s.mapname = *v;
    if (const std::string* v = get("gametype")) s.gametype = *v;
    s.clients = num("clients", 0);
    s.maxClients = num("sv_maxclients", 0);
    s.maxPing = num("maxping", -1);
    s.protocol = num("protocol", 0);
    s.pure = num("pure", -1);
    s.voice = num("voice", -1);
    s.password = num("pswrd", -1);
    s.mod = num("mod", -1);
    if (s.mod < 0)
        if (const std::string* v = get("fs_game")) s.mod = v->empty() ? 0 : 1;
    return !kv.empty();
}

static bool containsNoCase(const std::string& hay, const std::string& needle) {
    return lower(stripColors(hay)).find(lower(trim(needle))) != std::string::npos;
}

bool matchFilter(const BrowserServer& s, const BrowserFilter& f) {
    if (f.useMap && !containsNoCase(s.mapname, f.map)) return false;
    if (f.useGametype && !containsNoCase(s.gametype, f.gametype)) return false;
    auto flag = [](int v) { return v > 0; };
    if (f.usePure && flag(s.pure) != f.pure) return false;
    if (f.usePassword && flag(s.password) != f.password) return false;
    if (f.useMod && flag(s.mod) != f.mod) return false;
    if (f.useVoice && flag(s.voice) != f.voice) return false;
    auto cmp = [](int v, bool less, int x) { return less ? v < x : v > x; };
    if (f.useMaxPing && !cmp(s.maxPing < 0 ? 0 : s.maxPing, f.maxPingLess, f.maxPingValue)) return false;
    if (f.useMaxClients && !cmp(s.maxClients, f.maxClientsLess, f.maxClientsValue)) return false;
    if (f.usePlayers && !cmp(s.clients, f.playersLess, f.playersValue)) return false;
    if (f.notEmptyNotFull && (s.clients <= 0 || (s.maxClients > 0 && s.clients >= s.maxClients))) return false;
    if (f.useCountry) {
        bool any = false, hit = false;
        std::string c;
        for (size_t i = 0; i <= f.country.size(); ++i) {
            const char ch = i < f.country.size() ? f.country[i] : ',';
            if (ch == ',' || ch == ' ' || ch == ';') {
                c = trim(c);
                if (!c.empty()) {
                    any = true;
                    if (lower(c) == lower(s.country)) hit = true;
                }
                c.clear();
            } else {
                c += ch;
            }
        }
        if (any && !hit) return false;
    }
    return true;
}

static bool addrLess(const Endpoint& a, const Endpoint& b) {
    unsigned ia[4] = {}, ib[4] = {};
    auto parse = [](const std::string& h, unsigned* o) {
        size_t p = 0;
        for (int i = 0; i < 4; ++i) {
            o[i] = (unsigned)std::atoi(h.c_str() + p);
            p = h.find('.', p);
            if (p == std::string::npos) break;
            ++p;
        }
    };
    parse(a.host, ia);
    parse(b.host, ib);
    for (int i = 0; i < 4; ++i)
        if (ia[i] != ib[i]) return ia[i] < ib[i];
    return a.port < b.port;
}

bool browserLess(const BrowserServer& a, const BrowserServer& b, BrowserCol col) {
    // without colour codes and surrounding spaces, lower case
    auto strCmp = [](const std::string& x, const std::string& y) { return lower(trim(stripColors(x))).compare(lower(trim(stripColors(y)))); };
    int c = 0;
    switch (col) {
        case BrowserCol::Address: break;
        case BrowserCol::Ping: c = a.pingMs - b.pingMs; break;
        case BrowserCol::Name: c = strCmp(a.hostname, b.hostname); break;
        case BrowserCol::Map:
            c = strCmp(a.mapname, b.mapname);
            if (c == 0) c = strCmp(a.gametype, b.gametype);
            break;
        case BrowserCol::Players:
            c = a.clients - b.clients;
            if (c == 0) c = a.maxClients - b.maxClients;
            break;
        case BrowserCol::MaxPing: c = a.maxPing - b.maxPing; break;
        case BrowserCol::Mod: c = a.mod - b.mod; break;
        case BrowserCol::Pure: c = a.pure - b.pure; break;
        case BrowserCol::Password: c = a.password - b.password; break;
        default: break;
    }
    if (c != 0) return c < 0;
    return addrLess(a.ep, b.ep);
}

std::vector<Endpoint> queryMasters(const std::vector<Endpoint>& masters, int protocol, int timeoutMs,
                                   const std::atomic<bool>* cancel, bool* anyAnswer) {
    if (anyAnswer) *anyAnswer = false;
    std::vector<Endpoint> out;
    UdpMulti u;
    if (!u.ok()) return out;
    const std::string pkt = getServersPacket(protocol);
    std::vector<std::string> sent, waiting;  // addresses of the asked master servers / of those still sending a list
    for (const auto& m : masters) {
        sockaddr_in a{};
        if (!UdpMulti::resolve(m, a)) continue;
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        const std::string key = std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
        if (std::find(sent.begin(), sent.end(), key) != sent.end()) continue;
        if (u.sendTo(a, pkt)) {
            sent.push_back(key);
            waiting.push_back(key);
        }
    }
    std::set<std::string> seen;
    uint64_t last = tickMs();
    bool finished = false;  // at least one master server sent the full list
    while (!sent.empty() && !(cancel && cancel->load())) {
        // nothing has come for this long; once a master server is done we wait only 800 ms for the others,
        // once all are done, 300 ms more for packets arriving out of order (after \EOF)
        const int idle = waiting.empty() ? 300 : finished ? std::min(timeoutMs, 800) : timeoutMs;
        if (tickMs() - last > (uint64_t)idle) break;
        std::string data;
        Endpoint from;
        if (!u.recvFrom(data, from, 50)) continue;
        if (std::find(sent.begin(), sent.end(), addrKey(from)) == sent.end()) continue;
        const auto w = std::find(waiting.begin(), waiting.end(), addrKey(from));
        std::vector<Endpoint> got;
        const MasterEnd end = parseGetServersResponse(data, got);
        if (!got.empty() || end != MasterEnd::None) {
            if (anyAnswer) *anyAnswer = true;
            last = tickMs();
        }
        for (const auto& ep : got) {
            const std::string k = addrKey(ep);
            if (!seen.insert(k).second) continue;
            out.push_back(ep);
        }
        if (end == MasterEnd::List) {  // \EOF: this master server's list has ended
            finished = true;
            if (w != waiting.end()) waiting.erase(w);
        }
    }
    return out;
}

void queryInfos(const std::vector<Endpoint>& servers, int maxInFlight, int timeoutMs, const std::atomic<bool>* cancel,
                const std::function<void(const BrowserServer&)>& onServer,
                const std::function<void(int, int)>& onProgress) {
    UdpMulti u;
    if (!u.ok()) return;
    const int total = (int)servers.size();
    maxInFlight = std::max(1, std::min(maxInFlight, 500));
    struct Flight { size_t idx; uint64_t sent; int tries; };
    std::map<std::string, Flight> inflight;
    std::vector<size_t> retry;
    size_t next = 0;
    int done = 0;
    uint64_t lastProgress = 0;
    const std::string pkt = makeOob("getinfo xxx");
    auto send = [&](size_t idx, int tries) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(servers[idx].port);
        if (inet_pton(AF_INET, servers[idx].host.c_str(), &a.sin_addr) != 1 || !u.sendTo(a, pkt)) {
            ++done;
            return;
        }
        inflight[addrKey(servers[idx])] = Flight{idx, tickMs(), tries};
    };
    while (!(cancel && cancel->load())) {
        // new requests while there is room (at most 20 per round, so that replies can come in meanwhile)
        int burst = 0;
        while ((int)inflight.size() < maxInFlight && burst < 20) {
            if (!retry.empty()) {
                const size_t i = retry.back();
                retry.pop_back();
                send(i, 2);
            } else if (next < servers.size()) {
                send(next++, 1);
            } else {
                break;
            }
            ++burst;
        }
        if (inflight.empty() && next >= servers.size() && retry.empty()) break;
        std::string data;
        Endpoint from;
        while (u.recvFrom(data, from, 10)) {
            auto it = inflight.find(addrKey(from));
            if (it == inflight.end()) continue;
            BrowserServer s;
            s.ep = servers[it->second.idx];
            s.pingMs = (int)std::min<uint64_t>(999, tickMs() - it->second.sent);
            inflight.erase(it);
            ++done;
            if (parseInfoResponse(data, s) && onServer) onServer(s);
            if (cancel && cancel->load()) break;
        }
        // expired requests: once more, then skipped
        const uint64_t now = tickMs();
        for (auto it = inflight.begin(); it != inflight.end();) {
            if (now - it->second.sent > (uint64_t)timeoutMs) {
                if (it->second.tries < 2) retry.push_back(it->second.idx);
                else ++done;
                it = inflight.erase(it);
            } else {
                ++it;
            }
        }
        if (onProgress && now - lastProgress > 200) {
            lastProgress = now;
            onProgress(done, total);
        }
    }
    if (onProgress) onProgress(done, total);
}

}  // namespace miniadmin
