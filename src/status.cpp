#include "miniadmin/status.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace miniadmin {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string ServerInfo::get(const std::string& key, const std::string& def) const {
    auto it = cvars.find(lower(key));
    return it == cvars.end() ? def : it->second;
}

int ServerInfo::maxClients() const {
    return std::atoi(get("sv_maxclients", get("sv_maxClients", "0")).c_str());
}
int ServerInfo::privateClients() const { return std::atoi(get("sv_privateclients", "0").c_str()); }
std::string ServerInfo::playerCountText() const {
    const int mx = maxClients(), pv = privateClients();
    const std::string n = std::to_string(players.size());
    if (pv <= 0 || pv >= mx) return n + "/" + std::to_string(mx);
    return n + "/" + std::to_string(mx - pv) + "(+" + std::to_string(pv) + ")";
}
int ServerInfo::maxPing() const { return std::atoi(get("sv_maxping", "0").c_str()); }
bool ServerInfo::passworded() const { return get("pswrd") == "1"; }
bool ServerInfo::punkbuster() const { return get("sv_punkbuster") == "1"; }

std::string stripColors(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '^' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1])) {
            ++i;
            continue;
        }
        out += s[i];
    }
    return out;
}

static std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { v.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

static bool parsePlayerLine(const std::string& line, Player& p) {
    std::istringstream is(line);
    int score, ping;
    if (!(is >> score >> ping)) return false;
    size_t q1 = line.find('"');
    size_t q2 = line.rfind('"');
    if (q1 == std::string::npos || q2 == q1) return false;
    p.score = score;
    p.ping = ping;
    p.name = line.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

bool parseStatusResponse(const std::string& raw, ServerInfo& out) {
    std::string body = raw;
    if (body.size() >= 4 && body.compare(0, 4, std::string(4, '\xFF')) == 0)
        body.erase(0, 4);

    auto lines = splitLines(body);
    if (lines.size() < 2 || lines[0].compare(0, 14, "statusResponse") != 0) return false;

    out = ServerInfo{};

    // line 2: \key\value\key\value...
    const std::string& info = lines[1];
    std::vector<std::string> tok;
    {
        std::string cur;
        bool started = false;
        for (char c : info) {
            if (c == '\\') {
                if (started) tok.push_back(cur);
                cur.clear();
                started = true;
            } else cur += c;
        }
        if (started) tok.push_back(cur);
    }
    for (size_t i = 0; i + 1 < tok.size(); i += 2) out.cvars[lower(tok[i])] = tok[i + 1];

    // Player lines
    int slot = 0;
    for (size_t i = 2; i < lines.size(); ++i) {
        Player p;
        if (parsePlayerLine(lines[i], p)) {
            p.slot = slot++;
            out.players.push_back(std::move(p));
        }
    }

    out.game = gameFromGamename(out.get("gamename"));
    return true;
}

}  // namespace miniadmin
