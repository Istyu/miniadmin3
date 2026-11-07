#include "miniadmin/monitor.hpp"

#include <algorithm>
#include <cstdlib>

namespace miniadmin {

Snapshot pollServer(const Endpoint& ep, int timeoutMs) {
    Snapshot s;
    UdpSocket u;
    if (!u.connectTo(ep)) return s;
    const uint64_t t0 = tickMs();
    u.send(makeOob("getstatus\n"));
    const std::string raw = u.receive(timeoutMs);
    const uint64_t t1 = tickMs();
    if (raw.empty() || !parseStatusResponse(raw, s.info)) return s;
    s.online = true;
    s.pingMs = (int)std::min<uint64_t>(t1 - t0, 999);
    return s;
}

bool parseEndpoint(const std::string& text, Endpoint& out) {
    auto a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return false;
    auto b = text.find_last_not_of(" \t\r\n");
    std::string t = text.substr(a, b - a + 1);

    Endpoint ep;
    size_t c = t.rfind(':');
    if (c == std::string::npos) {
        ep.host = t;
    } else {
        ep.host = t.substr(0, c);
        const std::string ps = t.substr(c + 1);
        if (ps.empty() || ps.find_first_not_of("0123456789") != std::string::npos || ps.size() > 5)
            return false;
        long p = std::atol(ps.c_str());
        if (p < 1 || p > 65535) return false;
        ep.port = (uint16_t)p;
    }
    if (ep.host.empty() || ep.host.find_first_of(" \t") != std::string::npos) return false;
    out = ep;
    return true;
}

bool extractIpPort(const std::string& t, Endpoint& out) {
    const size_t n = t.size();
    auto digit = [&](size_t i) { return i < n && t[i] >= '0' && t[i] <= '9'; };
    for (size_t start = 0; start < n; ++start) {
        if (!digit(start) || (start > 0 && digit(start - 1))) continue;  // start of the first octet: no digit before it
        size_t p = start;
        int oct[4];
        bool ok = true;
        for (int k = 0; k < 4 && ok; ++k) {
            size_t b = p;
            int v = 0;
            while (digit(p) && p - b < 3) v = v * 10 + (t[p++] - '0');
            if (p == b || v > 255) { ok = false; break; }
            oct[k] = v;
            if (k < 3) { if (p < n && t[p] == '.') ++p; else ok = false; }
        }
        if (!ok || p >= n || t[p] != ':') continue;
        ++p;
        size_t b = p;
        long port = 0;
        while (digit(p) && p - b < 6) port = port * 10 + (t[p++] - '0');
        if (p == b || port < 1 || port > 65535 || digit(p)) continue;  // a port of 6+ digits is not valid
        out.host = std::to_string(oct[0]) + "." + std::to_string(oct[1]) + "." + std::to_string(oct[2]) + "." +
                   std::to_string(oct[3]);
        out.port = (uint16_t)port;
        return true;
    }
    return false;
}

bool parseServerAddress(const std::string& text, Endpoint& out) {
    return extractIpPort(text, out) || parseEndpoint(text, out);
}

}  // namespace miniadmin
