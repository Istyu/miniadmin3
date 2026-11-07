#include "miniadmin/gametypes.hpp"

#include <algorithm>
#include <cctype>

namespace miniadmin {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool isIdent(const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

}  // namespace

bool parseGametypeFileName(const std::string& fileName, GtFile& out) {
    const size_t slash = fileName.find_last_of("/\\");
    std::string base = fileName.substr(slash == std::string::npos ? 0 : slash + 1);
    base = base.substr(0, base.find('.'));
    const size_t a = base.find('_');
    if (a == std::string::npos) return false;
    const size_t b = base.find('_', a + 1);
    if (b == std::string::npos || base.find('_', b + 1) != std::string::npos) return false;
    GtFile f;
    f.game = upper(base.substr(0, a));
    f.gametype = lower(base.substr(a + 1, b - a - 1));
    f.lang = lower(base.substr(b + 1));
    if (f.game.empty() || f.gametype.empty() || f.lang.empty()) return false;
    f.path = fileName;
    out = f;
    return true;
}

bool parseLangFileName(const std::string& fileName, GtFile& out) {
    const size_t slash = fileName.find_last_of("/\\");
    std::string base = fileName.substr(slash == std::string::npos ? 0 : slash + 1);
    base = base.substr(0, base.find('.'));
    GtFile f;
    const size_t a = base.find('_');
    if (a != std::string::npos) {
        if (base.find('_', a + 1) != std::string::npos) return false;
        f.game = upper(base.substr(0, a));
        f.lang = lower(base.substr(a + 1));
    } else {
        for (const char* g : {"CODUO", "COD1", "COD2", "COD4", "COD5"}) {
            const std::string t = g;
            if (base.size() > t.size() && upper(base.substr(0, t.size())) == t) {
                f.game = t;
                f.lang = lower(base.substr(t.size()));
                break;
            }
        }
    }
    if (f.game.empty() || f.lang.empty()) return false;
    f.path = fileName;
    out = f;
    return true;
}

std::vector<GtVar> parseGametypeList(const std::string& text) {
    std::vector<GtVar> out;
    std::vector<std::string> seen;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        const size_t semi = line.find(';');
        std::string name = trim(line.substr(0, semi));
        // the French COD4 codcfg also has "set sv_master1; value" lines: the name is the word after "set"
        // (the original sent the whole "set sv_master1" as the query, which emptied the variable)
        for (const char* kw : {"set ", "seta ", "sets ", "setu "}) {
            const std::string k = kw;
            if (lower(name.substr(0, k.size())) == k) { name = trim(name.substr(k.size())); break; }
        }
        const std::string ln = lower(name);
        if (ln == "set" || ln == "seta" || ln == "sets" || ln == "setu") continue;  // "set ;x": no variable name
        if (!isIdent(name)) continue;
        const std::string key = lower(name);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        out.push_back({name, semi == std::string::npos ? std::string() : trim(line.substr(semi + 1))});
    }
    return out;
}

std::string modDisplayName(const std::string& fileName, const std::string& ext) {
    const size_t slash = fileName.find_last_of("/\\");
    const std::string base = fileName.substr(slash == std::string::npos ? 0 : slash + 1);
    if (base.size() <= ext.size() + 2) return {};
    return base.substr(0, base.size() - ext.size() - 2);
}

bool parseModFileName(const std::string& fileName, const std::string& ext, GtFile& out) {
    const size_t slash = fileName.find_last_of("/\\");
    const std::string base = fileName.substr(slash == std::string::npos ? 0 : slash + 1);
    const std::string e = lower(ext);
    if (base.size() <= e.size() + 2 || lower(base.substr(base.size() - e.size())) != e) return false;
    const std::string stem = base.substr(0, base.size() - e.size());
    GtFile f;
    f.lang = lower(stem.substr(stem.size() - 2));
    const std::string mod = stem.substr(0, stem.size() - 2);
    if (mod.empty() || !std::isalpha((unsigned char)f.lang[0]) || !std::isalpha((unsigned char)f.lang[1])) return false;
    f.game = upper(mod);
    f.gametype.clear();  // (pickGametypeFile searches with an empty gametype)
    f.path = fileName;
    out = f;
    return true;
}

bool parsePamModes(const std::string& text, std::vector<std::string>& modes) {
    modes.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty()) continue;
        const bool ok = std::all_of(line.begin(), line.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '_' || c == '-';
        });
        if (!ok) { modes.clear(); return false; }
        if (std::find(modes.begin(), modes.end(), line) == modes.end()) modes.push_back(line);
    }
    return !modes.empty();
}

std::vector<std::string> gametypesOf(const std::vector<GtFile>& files, const std::string& game) {
    std::vector<std::string> v;
    const std::string g = upper(game);
    for (const auto& f : files)
        if (f.game == g && std::find(v.begin(), v.end(), f.gametype) == v.end()) v.push_back(f.gametype);
    std::sort(v.begin(), v.end());
    return v;
}

const GtFile* pickGametypeFile(const std::vector<GtFile>& files, const std::string& game, const std::string& gametype,
                               const std::string& lang) {
    const std::string g = upper(game), gt = lower(gametype), l = lower(lang);
    const GtFile *en = nullptr, *any = nullptr;
    for (const auto& f : files) {
        if (f.game != g || f.gametype != gt) continue;
        if (f.lang == l) return &f;
        if (!en && f.lang == "en") en = &f;
        if (!any) any = &f;
    }
    return en ? en : any;
}

std::vector<GtVar> builtinGametypeVars(const std::string& gtIn) {
    const std::string gt = lower(gtIn);
    const std::string p = "scr_" + gt + "_";
    std::vector<GtVar> v;
    auto add = [&](const std::string& suffix) { v.push_back({p + suffix, ""}); };
    add("scorelimit");
    add("timelimit");
    add("roundlimit");
    add("numlives");
    if (gt == "sd" || gt == "sab") {
        add("bombtimer");
        add("planttime");
        add("defusetime");
    }
    if (gt == "sd") {
        add("multibomb");
        add("roundswitch");
    }
    if (gt == "sab") add("hotpotato");
    add("playerrespawndelay");
    add("waverespawndelay");
    return v;
}

std::string gametypeVarKey(const std::string& varName, const std::string& gametype) {
    std::string n = lower(varName);
    const std::string p = "scr_" + lower(gametype) + "_";
    if (n.compare(0, p.size(), p) == 0) n.erase(0, p.size());
    return "gtvar." + n;
}

bool parseCvarReply(const std::string& reply, const std::string& name, std::string& value) {
    const std::string lr = lower(reply), key = "\"" + lower(name) + "\"";
    const size_t k = lr.find(key);
    if (k == std::string::npos) return false;
    size_t p = lr.find("is:", k + key.size());
    if (p == std::string::npos) p = lr.find(" is ", k + key.size());
    if (p == std::string::npos) return false;
    const size_t q1 = reply.find('"', p);
    if (q1 == std::string::npos) return false;
    const size_t q2 = reply.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    std::string v = reply.substr(q1 + 1, q2 - q1 - 1);
    if (v.size() >= 2 && v.compare(v.size() - 2, 2, "^7") == 0) v.erase(v.size() - 2);
    value = v;
    return true;
}

}  // namespace miniadmin
