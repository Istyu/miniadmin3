#include "miniadmin/rconparse.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace miniadmin {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { v.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

bool toInt(const std::string& t, int& v) {
    if (t.empty()) return false;
    size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    if (i >= t.size()) return false;
    for (size_t k = i; k < t.size(); ++k)
        if (!std::isdigit((unsigned char)t[k])) return false;
    v = std::atoi(t.c_str());
    return true;
}

struct Tok { size_t b, e; };

std::vector<Tok> tokenize(const std::string& s) {
    std::vector<Tok> t;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i >= s.size()) break;
        size_t b = i;
        while (i < s.size() && !std::isspace((unsigned char)s[i])) ++i;
        t.push_back({b, i});
    }
    return t;
}

}  // namespace

bool parseRconStatus(const std::string& text, RconStatus& out) {
    out = RconStatus{};
    bool any = false;
    size_t extra = 0;  // number of extra columns between guid and name (e.g. CoD4x steamid)
    for (const std::string& line : lines(text)) {
        if (line.compare(0, 4, "map:") == 0) {
            out.mapname = trim(line.substr(4));
            any = true;
            continue;
        }
        const auto tk = tokenize(line);
        auto s = [&](size_t i) { return line.substr(tk[i].b, tk[i].e - tk[i].b); };
        if (tk.size() >= 9 && s(0) == "num" && s(1) == "score") {  // header: the number of columns
            extra = tk.size() - 9;
            continue;
        }
        // num score ping guid [extra...] [name...] lastmsg address qport rate
        if (tk.size() < 8 + extra) continue;

        RconPlayer p;
        int num;
        if (!toInt(s(0), num) || !toInt(s(1), p.score)) continue;  // header / "---" line
        if (!toInt(s(2), p.ping)) p.ping = 999;                    // CNCT, ZMBI
        p.slot = num;
        p.guid = s(3);

        const size_t n = tk.size();
        toInt(s(n - 4), p.lastmsg);
        p.address = s(n - 3);
        toInt(s(n - 2), p.qport);
        toInt(s(n - 1), p.rate);

        const size_t nb = tk[3 + extra].e, ne = tk[n - 4].b;
        p.name = ne > nb ? trim(line.substr(nb, ne - nb)) : std::string();
        out.players.push_back(std::move(p));
        any = true;
    }
    return any;
}

std::vector<std::string> parseMapList(const std::string& dirOutput) {
    std::vector<std::string> maps;
    for (std::string l : lines(dirOutput)) {
        l = trim(l);
        std::string low = l;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        size_t extLen = 0;
        auto ends = [&](const char* e) {
            size_t n = std::string(e).size();
            return low.size() > n && low.compare(low.size() - n, n, e) == 0 ? (extLen = n, true) : false;
        };
        if (!ends(".d3dbsp") && !ends(".bsp")) continue;
        std::string name = l.substr(0, l.size() - extLen);
        size_t sl = name.find_last_of("/\\");
        if (sl != std::string::npos) name.erase(0, sl + 1);
        if (!name.empty()) maps.push_back(name);
    }
    std::sort(maps.begin(), maps.end());
    maps.erase(std::unique(maps.begin(), maps.end()), maps.end());
    return maps;
}

std::vector<std::string> parseCfgList(const std::string& dirOutput) {
    std::vector<std::string> out;
    for (std::string l : lines(dirOutput)) {
        l = trim(l);
        std::string low = l;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (low.size() <= 4 || low.compare(low.size() - 4, 4, ".cfg") != 0) continue;
        if (low.find("directory of") != std::string::npos || l.find(' ') != std::string::npos) continue;
        size_t sl = l.find_last_of("/\\");
        if (sl != std::string::npos) l.erase(0, sl + 1);
        if (l.size() > 4) out.push_back(l);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::string> mapsFromPictures(const std::vector<std::string>& pictures, const std::string& gameTag) {
    auto lower = [](std::string x) {
        std::transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return x;
    };
    const std::string tag = lower(gameTag);
    std::vector<std::string> out, seen;
    if (tag.empty()) return out;
    for (const auto& p : pictures) {
        if (p.size() <= tag.size() || lower(p.substr(0, tag.size())) != tag) continue;
        std::string name = p.substr(tag.size());
        const std::string low = lower(name);
        if (low == "nomap" || std::find(seen.begin(), seen.end(), low) != seen.end()) continue;
        seen.push_back(low);
        out.push_back(std::move(name));
    }
    return out;
}

}  // namespace miniadmin
