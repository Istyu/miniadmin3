#include "miniadmin/settings.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>

namespace miniadmin {

std::vector<GameExe> Settings::defaultExes() {
    // The original "Call of Duty install paths" table (game;protocol, version), in the same order.
    return {
        {"COD4", "2", "1.3", ""},   {"COD4", "1", "1.0", ""},   {"COD1", "5", "1.4", ""},   {"COD1", "6", "1.5", ""},
        {"CODUO", "21", "1.41", ""}, {"CODUO", "22", "1.51", ""}, {"COD2", "115", "1.0", ""}, {"COD2", "117", "1.2", ""},
        {"COD2", "118", "1.3", ""}, {"COD4", "3", "1.4", ""},   {"COD4", "4", "1.5", ""},   {"COD4", "5", "1.6", ""},
        {"COD4", "6", "1.7", ""},   {"COD5", "94", "1", ""},    {"COD5", "95", "1", ""},    {"COD5", "96", "1", ""},
        {"COD5", "97", "1", ""},    {"COD5", "98", "1", ""},    {"COD5", "99", "1", ""},
    };
}

static std::string stripEol(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

static int clampInt(const std::string& v, int lo, int hi, int def) {
    if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) return def;
    return std::min(hi, std::max(lo, std::atoi(v.c_str())));
}

static int signedInt(const std::string& v, int def) {
    if (v.empty() || v.find_first_not_of("-0123456789") != std::string::npos || v.find('-', 1) != std::string::npos) return def;
    return std::atoi(v.c_str());
}

static std::string lowerStr(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// valid UTF-8? (the original config.cfg is ANSI)
static bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + n >= s.size()) return false;
        for (int k = 1; k <= n; ++k)
            if (i + k >= s.size() || ((unsigned char)s[i + k] >> 6) != 2) return false;
        i += (size_t)n + 1;
    }
    return true;
}

// 1252 → UTF-8 (the 0x80–0x9F range according to Windows-1252)
static std::string cp1252ToUtf8Str(const std::string& s) {
    static const unsigned short hi[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                          0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                          0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                          0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    std::string o;
    for (unsigned char c : s) {
        unsigned u = c;
        if (c >= 0x80 && c < 0xA0) u = hi[c - 0x80];
        if (u < 0x80) o += (char)u;
        else if (u < 0x800) { o += (char)(0xC0 | (u >> 6)); o += (char)(0x80 | (u & 63)); }
        else { o += (char)(0xE0 | (u >> 12)); o += (char)(0x80 | ((u >> 6) & 63)); o += (char)(0x80 | (u & 63)); }
    }
    return o;
}

static const char* const kOrigLangs[] = {"Deutsch", "English", "French", "Hungarian", "Nederlands", "Spanish"};

std::string originalLanguageName(int index) {
    return (index >= 0 && index < 6) ? kOrigLangs[index] : std::string();
}
int originalLanguageIndex(const std::string& name) {
    for (int i = 0; i < 6; ++i)
        if (lowerStr(name) == lowerStr(kOrigLangs[i])) return i;
    return -1;
}

// which section holds the game's path (as in the original)
static const char* exeSection(const std::string& game) {
    if (game == "COD4") return "codexepath4";
    if (game == "COD5") return "codexepath5";
    return "codexepath";  // COD1, CODUO, COD2
}

bool Settings::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<GameExe> oldExes;  // the exe= lines of the old (section-less) settings.ini
    std::vector<Extra> ext;
    bool haveLangName = false;
    int langIndex = -1;
    bool haveExeSection = false;
    std::vector<GameExe> rows = defaultExes();
    std::string sec, line;
    while (std::getline(in, line)) {
        line = stripEol(line);
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            const size_t e = line.find(']');
            sec = lowerStr(line.substr(1, e == std::string::npos ? std::string::npos : e - 1));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), lk = lowerStr(k);
        std::string v = line.substr(eq + 1);  // the value's spaces are kept
        if (!validUtf8(v)) v = cp1252ToUtf8Str(v);
        const bool on = v == "1";
        bool known = true;
        if (sec.empty()) {  // the old own format
            if (k == "adminPrefix") adminPrefix = v;
            else if (k == "joinAction") joinAction = (JoinAction)clampInt(v, 0, 2, 0);
            else if (k == "refreshMode") refreshMode = (RefreshMode)clampInt(v, 0, 2, 0);
            else if (k == "launchParams") launchParams = v;
            else if (k == "joinCommand") joinCommand = v;
            else if (k == "addPosition") addPosition = (AddPosition)clampInt(v, 0, 2, 0);
            else if (k == "addAfter") addAfter = clampInt(v, 0, 9999, 3);
            else if (k == "logCommands") logCommands = on;
            else if (k == "language") { language = v; haveLangName = true; }
            else if (k == "pingLevels") {  // 20,40,60,80,100
                size_t a = 0;
                for (int i = 0; i < 5 && a <= v.size(); ++i) {
                    size_t c = v.find(',', a);
                    if (c == std::string::npos) c = v.size();
                    pingLevels[i] = clampInt(v.substr(a, c - a), 1, 999, pingLevels[i]);
                    a = c + 1;
                }
            }
            else if (k == "pingMeter") pingMeter = on;
            else if (k == "pingMeterColored") pingMeterColored = on;
            else if (k == "pingMeterMax") pingMeterMax = clampInt(v, 10, 999, 100);
            else if (k == "exe") {  // game \t protocol \t version \t path
                GameExe e;
                size_t a = 0;
                std::string* f[3] = {&e.game, &e.protocol, &e.version};
                bool ok = true;
                for (int i = 0; i < 3; ++i) {
                    const size_t t = v.find('\t', a);
                    if (t == std::string::npos) { ok = false; break; }
                    *f[i] = v.substr(a, t - a);
                    a = t + 1;
                }
                if (ok) {
                    e.path = v.substr(a);
                    oldExes.push_back(std::move(e));
                }
            }
            else known = false;
        }
        else if (sec == "join") {
            if (lk == "noop") { if (on) joinAction = JoinAction::KeepOpen; }
            else if (lk == "min") { if (on) joinAction = JoinAction::Minimize; }
            else if (lk == "quit") { if (on) joinAction = JoinAction::Close; }
            else known = false;
        }
        else if (sec == "refresh") {
            if (lk == "all") { if (on) refreshMode = RefreshMode::AllServers; }
            else if (lk == "one") { if (on) refreshMode = RefreshMode::SelectedOnly; }
            else if (lk == "2") { if (on) refreshMode = RefreshMode::TwoAtATime; }
            else known = false;
        }
        else if (sec == "serveradd") {
            if (lk == "1") { if (on) addPosition = AddPosition::Top; }
            else if (lk == "2") { if (on) addPosition = AddPosition::Bottom; }
            else if (lk == "3") { if (on) addPosition = AddPosition::AfterIndex; }
            else known = false;
        }
        else if (sec == "fade" && lk == "fade") fadeIn = on;
        else if (sec == "hide" && lk == "tray") trayMinimize = on;
        else if (sec == "save" && lk == "windowpos") saveWindowPos = on;
        else if (sec == "table" && lk == "colors") coloredNames = on;
        else if (sec == "style" && lk == "visual") ownVisualStyle = on;
        else if (sec == "start" && lk == "param") launchParams = v;
        else if (sec == "start" && lk == "command") joinCommand = v;
        else if (sec == "lineadd" && lk == "line") addAfter = clampInt(v, 0, 9999, 3);
        else if (sec == "formpos") {
            if (lk == "maximized") maximized = on;
            else if (lk == "left") winLeft = signedInt(v, winLeft);
            else if (lk == "top") winTop = signedInt(v, winTop);
            else if (lk == "width") winWidth = clampInt(v, 0, 20000, 0);
            else if (lk == "height") winHeight = clampInt(v, 0, 20000, 0);
            else known = false;
        }
        else if (sec == "logging" && lk == "cmds") logCommands = on;
        else if (sec == "lng" && lk == "index") langIndex = signedInt(v, -1);
        else if (sec == "lng" && lk == "name") { language = v; haveLangName = !v.empty(); }
        else if (sec == "admin" && lk == "nick") adminPrefix = v;
        else if (sec == "pam" && lk == "autocheckup") pamAutoCheck = on;
        else if (sec == "pinglevel" && lk.size() == 1 && lk[0] >= '1' && lk[0] <= '5')
            pingLevels[lk[0] - '1'] = clampInt(v, 1, 999, pingLevels[lk[0] - '1']);
        else if (sec == "ping" && lk == "maxlevel") pingMeterMax = clampInt(v, 10, 999, 100);
        else if (sec == "pingmeter" && lk == "enabled") pingMeter = on;
        else if (sec == "pingmeter" && lk == "colored") pingMeterColored = on;
        else if (sec == "masterserverlist" && lk == "threadcount") masterThreads = clampInt(v, 1, 999, 50);
        else if (sec == "masterserverlist" && lk == "version" && !v.empty()) masterVersion = v;
        else if (sec == "punkbuster" && lk == "kick_ban") pbKickBan = on;
        else if (sec == "ask" && lk == "delsrv") askDeleteServer = on;
        else if (sec == "delss" && lk == "serverdel") deleteSsOnServer = on;
        else if (sec == "cpu" && lk == "affinity") cpuAffinity = clampInt(v, 0, 3, 0);
        else if (sec == "codexepath" || sec == "codexepath4" || sec == "codexepath5") {
            haveExeSection = true;
            known = false;
            for (auto& e : rows)
                if (exeSection(e.game) == sec && e.protocol == k) { e.path = v; known = true; }
            // the original's old, no longer used [CodExePath] keys (1, 2, 3) are not needed
            if (!known && sec == "codexepath" && v.empty()) known = true;
        }
        else known = false;
        if (!known) ext.push_back({sec, k, v});
    }
    if (haveExeSection) exes = std::move(rows);
    if (!oldExes.empty()) {
        // old settings.ini: the exact rows, then the paths of the "*" rows into the game's empty rows
        std::vector<GameExe> r = defaultExes();
        for (const auto& o : oldExes) {
            if (o.path.empty() || o.protocol == "*") continue;
            bool found = false;
            for (auto& e : r)
                if (e.game == o.game && e.protocol == o.protocol) { e.path = o.path; found = true; }
            if (!found) r.push_back(o);
        }
        for (const auto& o : oldExes)
            if (!o.path.empty() && o.protocol == "*")
                for (auto& e : r)
                    if (e.game == o.game && e.path.empty()) e.path = o.path;
        exes = std::move(r);
    }
    if (!haveLangName && langIndex >= 0 && !originalLanguageName(langIndex).empty()) language = originalLanguageName(langIndex);
    // the section key (lower case) is kept for unknown sections
    extra = std::move(ext);
    normalizePing(*this);
    return true;
}

bool Settings::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const char* nl = "\r\n";
    auto b = [](bool v) { return v ? "1" : "0"; };
    auto extras = [&](const char* sec) {  // the unknown keys of the given section
        for (const auto& x : extra)
            if (x.section == lowerStr(sec)) out << x.key << '=' << x.value << nl;
    };
    auto section = [&](const char* name) { out << '[' << name << ']' << nl; };
    section("Join");
    out << "noop=" << b(joinAction == JoinAction::KeepOpen) << nl << "min=" << b(joinAction == JoinAction::Minimize) << nl
        << "quit=" << b(joinAction == JoinAction::Close) << nl;
    extras("Join");
    section("refresh");
    out << "all=" << b(refreshMode == RefreshMode::AllServers) << nl << "one=" << b(refreshMode == RefreshMode::SelectedOnly)
        << nl << "2=" << b(refreshMode == RefreshMode::TwoAtATime) << nl;
    extras("refresh");
    section("fade");      out << "fade=" << b(fadeIn) << nl;                    extras("fade");
    section("hide");      out << "tray=" << b(trayMinimize) << nl;              extras("hide");
    section("save");      out << "windowpos=" << b(saveWindowPos) << nl;        extras("save");
    section("table");     out << "colors=" << b(coloredNames) << nl;            extras("table");
    section("style");     out << "visual=" << b(ownVisualStyle) << nl;          extras("style");
    section("Start");     out << "param=" << launchParams << nl << "command=" << joinCommand << nl; extras("Start");
    section("lineadd");   out << "line=" << addAfter << nl;                     extras("lineadd");
    section("serveradd");
    out << "1=" << b(addPosition == AddPosition::Top) << nl << "2=" << b(addPosition == AddPosition::Bottom) << nl
        << "3=" << b(addPosition == AddPosition::AfterIndex) << nl;
    extras("serveradd");
    section("formpos");
    out << "maximized=" << b(maximized) << nl;
    if (winLeft > -30000 && winTop > -30000) out << "left=" << winLeft << nl << "top=" << winTop << nl;
    if (winWidth > 0 && winHeight > 0) out << "width=" << winWidth << nl << "height=" << winHeight << nl;
    extras("formpos");
    section("logging");   out << "cmds=" << b(logCommands) << nl;               extras("logging");
    section("LNG");
    if (originalLanguageIndex(language) >= 0) out << "index=" << originalLanguageIndex(language) << nl;
    out << "name=" << language << nl;
    extras("LNG");
    section("admin");     out << "nick=" << adminPrefix << nl;                  extras("admin");
    section("pam");       out << "autocheckup=" << b(pamAutoCheck) << nl;       extras("pam");
    section("pinglevel");
    for (int i = 0; i < 5; ++i) out << (i + 1) << '=' << pingLevels[i] << nl;
    extras("pinglevel");
    section("ping");      out << "maxlevel=" << pingMeterMax << nl;             extras("ping");
    section("pingmeter"); out << "enabled=" << b(pingMeter) << nl << "colored=" << b(pingMeterColored) << nl; extras("pingmeter");
    for (const char* sec : {"CodExePath", "CodExePath4", "CodExePath5"}) {
        if (std::strcmp(sec, "CodExePath4") == 0) {  // (the original's order)
            section("masterserverlist");
            out << "threadcount=" << masterThreads << nl;
            out << "version=" << masterVersion << nl;
            extras("masterserverlist");
        }
        section(sec);
        for (const auto& e : exes)
            if (exeSection(e.game) == lowerStr(sec) && e.protocol != "*") out << e.protocol << '=' << e.path << nl;
        extras(sec);
        if (std::strcmp(sec, "CodExePath4") == 0) {
            section("punkbuster"); out << "kick_ban=" << b(pbKickBan) << nl;        extras("punkbuster");
            section("ask");        out << "delsrv=" << b(askDeleteServer) << nl;    extras("ask");
            section("delss");      out << "serverdel=" << b(deleteSsOnServer) << nl; extras("delss");
            section("cpu");        out << "affinity=" << cpuAffinity << nl;         extras("cpu");
        }
    }
    // the remaining unknown sections
    static const char* const kKnown[] = {"join", "refresh", "fade", "hide", "save", "table", "style", "start", "lineadd",
                                         "serveradd", "formpos", "logging", "lng", "admin", "pam", "pinglevel", "ping",
                                         "pingmeter", "codexepath", "codexepath4", "codexepath5", "masterserverlist",
                                         "punkbuster", "ask", "delss", "cpu", ""};
    std::vector<std::string> done;
    for (const auto& x : extra) {
        const bool isKnown = std::any_of(std::begin(kKnown), std::end(kKnown), [&](const char* s) { return x.section == s; });
        if (isKnown || std::find(done.begin(), done.end(), x.section) != done.end()) continue;
        done.push_back(x.section);
        section(x.section.c_str());
        for (const auto& y : extra)
            if (y.section == x.section) out << y.key << '=' << y.value << nl;
    }
    return (bool)out;
}

const GameExe* Settings::findExe(Game g, const std::string& protocol) const {
    const std::string tag = gameTag(g);
    if (g == Game::Unknown) return nullptr;
    for (const auto& e : exes)
        if (e.game == tag && !e.path.empty() && e.protocol == protocol && !protocol.empty()) return &e;
    for (const auto& e : exes)
        if (e.game == tag && !e.path.empty() && e.protocol == "*") return &e;
    for (const auto& e : exes)
        if (e.game == tag && !e.path.empty()) return &e;
    return nullptr;
}

std::string buildLaunchCommandLine(const std::string& exePath, const Endpoint& ep, const std::string& extra) {
    std::string c = "\"" + exePath + "\" +connect " + ep.host + ":" + std::to_string(ep.port);
    if (!extra.empty()) c += " " + extra;
    return c;
}

static std::string noQuotes(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
    return s;
}

std::string buildJoinCommandLine(const std::string& exePath, const Endpoint& ep, const std::string& name,
                                 const std::string& password, const std::string& extra) {
    std::string c = "\"" + exePath + "\"";
    if (!noQuotes(name).empty()) c += " +name \"" + noQuotes(name) + "\"";
    if (!password.empty()) c += " +password \"" + noQuotes(password) + "\"";
    c += " +connect " + ep.host + ":" + std::to_string(ep.port);
    if (!extra.empty()) c += " " + extra;
    return c;
}

void pushHistory(std::vector<std::string>& list, const std::string& item, size_t maxItems) {
    if (item.empty()) return;
    list.erase(std::remove(list.begin(), list.end(), item), list.end());
    list.insert(list.begin(), item);
    if (list.size() > maxItems) list.resize(maxItems);
}

std::vector<std::string> loadHistory(const std::string& path) {
    std::vector<std::string> v;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        line = stripEol(line);
        if (!line.empty() && std::find(v.begin(), v.end(), line) == v.end()) v.push_back(line);
    }
    return v;
}

bool saveHistory(const std::string& path, const std::vector<std::string>& list) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const auto& s : list) out << s << "\r\n";
    return (bool)out;
}

std::string messagePrefix(const std::string& prefix) {
    size_t n = prefix.size();
    while (n >= 2 && prefix[n - 2] == '^' && prefix[n - 1] >= '0' && prefix[n - 1] <= '9') n -= 2;
    if (n == 0) return prefix;  // only colour codes: no visible name
    const char last = prefix[n - 1];
    if (last == ' ' || last == '\t') return prefix;
    return prefix + " ";
}

int insertIndex(const Settings& s, int count) {
    switch (s.addPosition) {
        case AddPosition::Top: return 0;
        case AddPosition::Bottom: return count;
        case AddPosition::AfterIndex: return std::min(std::max(s.addAfter, 0), count);
    }
    return count;
}

int pingLevelIndex(const Settings& s, int pingMs) {
    for (int i = 0; i < 5; ++i)
        if (pingMs <= s.pingLevels[i]) return i;
    return 5;
}

void normalizePing(Settings& s) {
    for (int i = 0; i < 5; ++i) {
        s.pingLevels[i] = std::min(999, std::max(1, s.pingLevels[i]));
        if (i > 0 && s.pingLevels[i] < s.pingLevels[i - 1]) s.pingLevels[i] = s.pingLevels[i - 1];
    }
    s.pingMeterMax = std::min(999, std::max(10, s.pingMeterMax));
}

}  // namespace miniadmin
