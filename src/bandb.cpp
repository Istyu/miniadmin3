#include "miniadmin/bandb.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace miniadmin {

namespace {

struct Reader {
    const std::string& d;
    size_t p;
    bool ok = true;
    Reader(const std::string& data, size_t pos) : d(data), p(pos) {}
    bool need(size_t n) {
        if (!ok || p + n > d.size()) ok = false;
        return ok;
    }
    uint8_t u8() { return need(1) ? (uint8_t)d[p++] : 0; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = (uint8_t)d[p] | ((uint8_t)d[p + 1] << 8) | ((uint8_t)d[p + 2] << 16) | ((uint32_t)(uint8_t)d[p + 3] << 24);
        p += 4;
        return v;
    }
    std::string bytes(size_t n) {
        if (!need(n)) return {};
        std::string s = d.substr(p, n);
        p += n;
        return s;
    }
    std::string shortStr() { return bytes(u8()); }
};

std::string latin1ToUtf8(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c < 0x80) o += (char)c;
        else { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}

std::string utf16ToUtf8(const std::string& b) {
    std::string o;
    for (size_t i = 0; i + 1 < b.size(); i += 2) {
        uint32_t c = (uint8_t)b[i] | ((uint8_t)b[i + 1] << 8);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < b.size()) {
            const uint32_t lo = (uint8_t)b[i + 2] | ((uint8_t)b[i + 3] << 8);
            if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
        }
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}

// x87 Extended (10 bytes) <-> double
double extendedToDouble(const std::string& b) {
    uint64_t mant = 0;
    for (int i = 7; i >= 0; --i) mant = (mant << 8) | (uint8_t)b[(size_t)i];
    const uint16_t se = (uint16_t)((uint8_t)b[8] | ((uint8_t)b[9] << 8));
    const int exp = se & 0x7FFF;
    if (exp == 0 && mant == 0) return 0.0;
    const double v = std::ldexp((double)mant, exp - 16383 - 63);
    return (se & 0x8000) ? -v : v;
}

std::string doubleToExtended(double d) {
    std::string o(10, '\0');
    if (d == 0.0 || !std::isfinite(d)) return o;
    const bool neg = d < 0;
    int e = 0;
    const double m = std::frexp(neg ? -d : d, &e);  // m in [0.5, 1)
    const uint64_t mant = (uint64_t)std::ldexp(m, 64);
    for (int i = 0; i < 8; ++i) o[(size_t)i] = (char)((mant >> (8 * i)) & 0xFF);
    const uint16_t se = (uint16_t)((e - 1 + 16383) | (neg ? 0x8000 : 0));
    o[8] = (char)(se & 0xFF);
    o[9] = (char)(se >> 8);
    return o;
}

void putU32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s += (char)((v >> (8 * i)) & 0xFF);
}

void putShort(std::string& s, const std::string& v) {
    s += (char)std::min<size_t>(v.size(), 255);
    s += v.substr(0, 255);
}

void putStringProp(std::string& s, const char* name, const std::string& v) {
    putShort(s, name);
    bool ascii = v.size() <= 255;
    for (unsigned char c : v)
        if (c >= 0x80) ascii = false;
    if (ascii) { s += (char)0x06; putShort(s, v); }  // vaString
    else { s += (char)0x14; putU32(s, (uint32_t)v.size()); s += v; }  // vaUTF8String
}

// Reads the value of a property. Text is converted to UTF-8; non-text types are skipped.
bool readValue(Reader& r, std::string& textOut, double& extOut, bool& isText, bool& isExt) {
    isText = isExt = false;
    const uint8_t t = r.u8();
    switch (t) {
        case 0: case 8: case 9: case 13: return r.ok;
        case 2: r.bytes(1); return r.ok;
        case 3: r.bytes(2); return r.ok;
        case 4: case 15: r.bytes(4); return r.ok;
        case 16: case 17: case 19: r.bytes(8); return r.ok;
        case 5: { const std::string b = r.bytes(10); if (!r.ok) return false; extOut = extendedToDouble(b); isExt = true; return true; }
        case 6: textOut = latin1ToUtf8(r.shortStr()); isText = true; return r.ok;
        case 7: r.shortStr(); return r.ok;
        case 12: { const uint32_t n = r.u32(); textOut = latin1ToUtf8(r.bytes(n)); isText = true; return r.ok; }
        case 18: { const uint32_t n = r.u32(); if (n > (1u << 20)) return false; textOut = utf16ToUtf8(r.bytes((size_t)n * 2)); isText = true; return r.ok; }
        case 20: { const uint32_t n = r.u32(); textOut = r.bytes(n); isText = true; return r.ok; }
        case 10: { const uint32_t n = r.u32(); r.bytes(n); return r.ok; }
        default: return false;  // list, collection, set: does not occur in this file
    }
}

bool parseEntry(Reader& r, BanRec& out) {
    const size_t start = r.p;
    if (r.bytes(4) != "TPF0") return false;
    if (r.shortStr() != "Tban") return false;
    r.shortStr();  // component name (empty)
    BanRec rec;
    for (;;) {
        const std::string name = r.shortStr();
        if (!r.ok) return false;
        if (name.empty()) break;
        std::string text;
        double ext = 0;
        bool isText, isExt;
        if (!readValue(r, text, ext, isText, isExt)) return false;
        if (name == "Pn" && isText) rec.nick = text;
        else if (name == "Pg" && isText) rec.guid = text;
        else if (name == "Pi" && isText) rec.server = text;
        else if (name == "Pd" && isExt) rec.when = ext;
    }
    if (r.u8() != 0 || !r.ok) return false;  // end of the child component list
    rec.raw = r.d.substr(start, r.p - start);
    out = std::move(rec);
    return true;
}

}  // namespace

std::string serializeBan(const BanRec& r) {
    std::string s = "TPF0";
    putShort(s, "Tban");
    putShort(s, "");
    putStringProp(s, "Pn", r.nick);
    putStringProp(s, "Pg", r.guid);
    putStringProp(s, "Pi", r.server);
    putShort(s, "Pd");
    s += (char)0x05;  // vaExtended
    s += doubleToExtended(r.when);
    s += (char)0;  // end of the properties
    s += (char)0;  // no child
    return s;
}

bool BanDb::load(const std::string& path) {
    recs_.clear();
    dirty_ = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Reader r(data, 0);
    const uint32_t n = r.u32();
    if (!r.ok || n > 100000) return false;
    std::vector<BanRec> v;
    for (uint32_t i = 0; i < n; ++i) {
        BanRec b;
        if (!parseEntry(r, b)) return false;
        v.push_back(std::move(b));
    }
    recs_ = std::move(v);
    return true;
}

bool BanDb::save(const std::string& path) const {
    std::string out;
    putU32(out, (uint32_t)recs_.size());
    for (const auto& b : recs_) out += b.raw.empty() ? serializeBan(b) : b.raw;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(out.data(), (std::streamsize)out.size());
        f.flush();
        if (!f) { f.close(); std::remove(tmp.c_str()); return false; }
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { std::remove(tmp.c_str()); return false; }
    return true;
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
    return true;
#endif
}

bool BanDb::add(const std::string& nick, const std::string& guid, const std::string& server, double when) {
    if (guid.size() < 10) return false;
    BanRec b;
    b.nick = nick;
    b.guid = guid;
    b.server = server;
    b.when = when;
    recs_.push_back(std::move(b));
    dirty_ = true;
    return true;
}

bool BanDb::removeAt(size_t i) {
    if (i >= recs_.size()) return false;
    recs_.erase(recs_.begin() + (std::ptrdiff_t)i);
    dirty_ = true;
    return true;
}

}  // namespace miniadmin
