#include "miniadmin/shortcuts.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace miniadmin {

namespace {

const char* const kSections[kShortcutCount] = {"fastrestart", "reload", "change", "gametype", "killcod"};
const char kDefaultKeys[kShortcutCount] = {'F', 'R', 'M', 'G', 'K'};

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

char cleanKey(const std::string& v) {
    const std::string t = trim(v);
    if (t.size() != 1) return 0;
    const char c = (char)std::toupper((unsigned char)t[0]);
    return std::isalnum((unsigned char)c) ? c : 0;
}

// ---- Delphi component stream (only as much as Tshortmapitem needs)
struct Reader {
    const std::string& d;
    size_t p = 0;
    bool ok = true;
    explicit Reader(const std::string& s) : d(s) {}
    unsigned char byte() {
        if (p >= d.size()) { ok = false; return 0; }
        return (unsigned char)d[p++];
    }
    std::string bytes(size_t n) {
        if (p + n > d.size()) { ok = false; return {}; }
        std::string s = d.substr(p, n);
        p += n;
        return s;
    }
    std::string shortStr() { return bytes(byte()); }
    uint32_t u32() {
        const std::string b = bytes(4);
        if (!ok) return 0;
        return (uint32_t)(unsigned char)b[0] | ((uint32_t)(unsigned char)b[1] << 8) | ((uint32_t)(unsigned char)b[2] << 16) |
               ((uint32_t)(unsigned char)b[3] << 24);
    }
    // a property value: vaString (6) or vaLString (12); others cannot be skipped
    bool value(std::string& out) {
        const unsigned char t = byte();
        if (t == 6) { out = shortStr(); return ok; }
        if (t == 12) { out = bytes(u32()); return ok; }
        ok = false;
        return false;
    }
};

void putStr(std::string& o, const std::string& s) {  // vaString or vaLString
    if (s.size() <= 255) {
        o += (char)6;
        o += (char)s.size();
    } else {
        o += (char)12;
        const uint32_t n = (uint32_t)s.size();
        for (int i = 0; i < 4; ++i) o += (char)((n >> (i * 8)) & 0xff);
    }
    o += s;
}
void putShort(std::string& o, const std::string& s) {
    o += (char)s.size();
    o += s;
}

}  // namespace

unsigned Hotkey::modifiers() const {
    static const unsigned m[4] = {0x0001 /*MOD_ALT*/, 0x0002 /*MOD_CONTROL*/, 0x0004 /*MOD_SHIFT*/, 0x0008 /*MOD_WIN*/};
    return mod >= 0 && mod <= 3 ? m[mod] : 0;
}

ShortcutConfig::ShortcutConfig() {
    for (int i = 0; i < kShortcutCount; ++i) keys[i] = Hotkey{1, kDefaultKeys[i]};
}

bool loadShortcuts(const std::string& path, ShortcutConfig& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    ShortcutConfig c;
    std::string line, sec;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') {
            sec = lower(line.substr(1, line.find(']') == std::string::npos ? std::string::npos : line.find(']') - 1));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
        if (sec == "master" && k == "enabled") { c.enabled = v == "1"; continue; }
        for (int i = 0; i < kShortcutCount; ++i) {
            if (sec != kSections[i]) continue;
            if (k == "ctrl") {
                const int m = std::atoi(v.c_str());
                c.keys[i].mod = (m >= 0 && m <= 4 && !v.empty()) ? m : 1;
            } else if (k == "key") {
                c.keys[i].key = cleanKey(v);
            }
        }
    }
    out = c;
    return true;
}

bool saveShortcuts(const std::string& path, const ShortcutConfig& c) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    auto key = [&](int i) {
        out << '[' << kSections[i] << "]\r\nctrl=" << c.keys[i].mod << "\r\nkey=";
        if (c.keys[i].key) out << c.keys[i].key;
        out << "\r\n";
    };
    key(0);
    key(1);
    key(2);
    out << "[master]\r\nenabled=" << (c.enabled ? 1 : 0) << "\r\n";  // (the original's order)
    key(3);
    key(4);
    return (bool)out;
}

bool loadMapAliases(const std::string& path, std::vector<MapAlias>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Reader r(data);
    const uint32_t n = r.u32();
    if (!r.ok || n > 100000) return false;
    std::vector<MapAlias> list;
    for (uint32_t i = 0; i < n && r.ok; ++i) {
        if (r.bytes(4) != "TPF0") return false;
        r.shortStr();  // class name (Tshortmapitem)
        r.shortStr();  // component name (empty)
        MapAlias a;
        for (;;) {
            const std::string prop = r.shortStr();
            if (!r.ok) return false;
            if (prop.empty()) break;  // end of the properties
            std::string v;
            if (!r.value(v)) return false;
            if (prop == "Pmap") a.map = v;
            else if (prop == "Pmapalias") a.alias = v;
        }
        r.byte();  // end of the child elements
        if (!r.ok) return false;
        if (!a.map.empty() && !a.alias.empty()) list.push_back(a);
    }
    out = std::move(list);
    return true;
}

bool saveMapAliases(const std::string& path, const std::vector<MapAlias>& list) {
    std::string o;
    const uint32_t n = (uint32_t)list.size();
    for (int i = 0; i < 4; ++i) o += (char)((n >> (i * 8)) & 0xff);
    for (const auto& a : list) {
        o += "TPF0";
        putShort(o, "Tshortmapitem");
        putShort(o, "");
        std::string doubled;
        for (char c : a.alias) { doubled += c; doubled += c; }
        putShort(o, "Pmapalias");  putStr(o, a.alias);
        putShort(o, "Pmapalias2"); putStr(o, doubled);
        putShort(o, "Pmap");       putStr(o, a.map);
        o += '\0';  // end of the properties
        o += '\0';  // end of the child elements
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << o;
    return (bool)out;
}

std::string mapForAlias(const std::vector<MapAlias>& list, const std::string& typed) {
    const std::string t = lower(trim(typed));
    if (t.empty()) return {};
    for (const auto& a : list)
        if (lower(a.alias) == t) return a.map;
    for (const auto& a : list)
        if (lower(a.map) == t) return a.map;
    // a map name is fine even if it is not in the list (e.g. "mp_crash")
    if (t.compare(0, 3, "mp_") == 0 &&
        t.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos)
        return t;
    return {};
}

std::string cleanGametype(const std::string& typed) {
    const std::string t = lower(trim(typed));
    if (t.empty() || t.size() > 20 || t.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
        return {};
    return t;
}

}  // namespace miniadmin
