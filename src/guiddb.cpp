#include "miniadmin/guiddb.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

#include "miniadmin/status.hpp"  // stripColors

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace miniadmin {

namespace {
constexpr size_t kGuidOff = 0, kGuidMax = 32, kNameOff = 33, kNameMax = 40, kIpOff = 74, kIpMax = 15, kDateOff = 90;

std::string readShort(const unsigned char* base, size_t off, size_t maxLen) {
    const size_t len = std::min<size_t>(base[off], maxLen);
    return std::string(reinterpret_cast<const char*>(base + off + 1), len);
}

void writeShort(unsigned char* base, size_t off, size_t maxLen, const std::string& s) {
    const size_t len = std::min(s.size(), maxLen);
    std::memset(base + off, 0, maxLen + 1);
    base[off] = (unsigned char)len;
    std::memcpy(base + off + 1, s.data(), len);
}

// Lower-casing for ASCII and for the Windows-1252 / Latin-1 accented capitals too (Á -> á, Ő -> ő ...),
// because the player names in the file use this encoding.
std::string lower(std::string s) {
    for (char& ch : s) {
        const unsigned char c = (unsigned char)ch;
        if (c >= 'A' && c <= 'Z') ch = (char)(c + 32);
        else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) ch = (char)(c + 32);
    }
    return s;
}

bool validGuid(const std::string& g) {
    if (g.empty() || g.size() > kGuidMax || g.find_first_of(" \t\r\n") != std::string::npos) return false;
    return g.find_first_not_of('0') != std::string::npos;
}

// Civil date <-> day number (1970-01-01 = 0), Howard Hinnant's algorithm.
long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

void civilFromDays(long long z, long long& y, unsigned& m, unsigned& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (long long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}
const long long kDelphiEpoch = daysFromCivil(1899, 12, 30);  // TDateTime 0
}  // namespace

std::string GuidRec::guid() const { return readShort(raw.data(), kGuidOff, kGuidMax); }
std::string GuidRec::name() const { return readShort(raw.data(), kNameOff, kNameMax); }
std::string GuidRec::ip() const { return readShort(raw.data(), kIpOff, kIpMax); }
double GuidRec::lastSeen() const {
    double d;
    std::memcpy(&d, raw.data() + kDateOff, sizeof d);
    return d;
}
void GuidRec::set(const std::string& g, const std::string& n, const std::string& i, double t) {
    writeShort(raw.data(), kGuidOff, kGuidMax, g);
    writeShort(raw.data(), kNameOff, kNameMax, n);
    writeShort(raw.data(), kIpOff, kIpMax, i);
    std::memcpy(raw.data() + kDateOff, &t, sizeof t);
}

void GuidDb::reindex() {
    index_.clear();
    for (size_t i = 0; i < recs_.size(); ++i) index_[recs_[i].guid()] = i;
}

void GuidDb::clear() {
    recs_.clear();
    index_.clear();
    dirty_ = true;
}

bool GuidDb::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0 || (size_t)size % GuidRec::kSize != 0) return false;
    in.seekg(0);
    std::vector<GuidRec> v((size_t)size / GuidRec::kSize);
    for (auto& r : v) {
        in.read(reinterpret_cast<char*>(r.raw.data()), (std::streamsize)GuidRec::kSize);
        if (!in) return false;
        // the length bytes must not be larger than the field size: otherwise it is not this format
        if (r.raw[kGuidOff] > kGuidMax || r.raw[kNameOff] > kNameMax || r.raw[kIpOff] > kIpMax) return false;
    }
    recs_ = std::move(v);
    reindex();
    dirty_ = false;
    return true;
}

bool GuidDb::save(const std::string& path) const {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        for (const auto& r : recs_) out.write(reinterpret_cast<const char*>(r.raw.data()), (std::streamsize)GuidRec::kSize);
        out.flush();
        if (!out) { out.close(); std::remove(tmp.c_str()); return false; }
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { std::remove(tmp.c_str()); return false; }
    return true;
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
    return true;
#endif
}

// The original's rule (sub_520A14 / sub_520A90): a new GUID may contain only letters and digits, at least 6 characters.
// An all-zero GUID (bots, unauthenticated players) is not added, unlike in the original.
static bool newGuidOk(const std::string& g) {
    if (g.size() < 6 || g.size() > kGuidMax) return false;
    for (unsigned char c : g)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
    return g.find_first_not_of('0') != std::string::npos;
}

static std::string trimSpaces(const std::string& s) {  // like Delphi's Trim: the leading / trailing characters ≤ ' '
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

bool GuidDb::touch(const std::string& guidIn, const std::string& nameIn, const std::string& ip, double lastSeen) {
    const std::string guid = trimSpaces(guidIn), name = trimSpaces(nameIn);
    auto it = index_.find(guid);
    if (it == index_.end()) {
        // new record: with the original's conditions (valid GUID, non-empty name)
        if (!newGuidOk(guid) || name.empty()) return false;
        GuidRec r;
        r.set(guid, name, ip, lastSeen);
        recs_.push_back(r);
        index_[guid] = recs_.size() - 1;
        dirty_ = true;
        return true;
    }
    // existing GUID: as in the original, the name stays (the one first seen), only the IP and the time are updated
    GuidRec& r = recs_[it->second];
    const std::string i = ip.substr(0, kIpMax);
    const bool changed = r.ip() != i;
    const bool newer = lastSeen - r.lastSeen() >= 60.0 / 86400.0;
    if (!changed && !newer) return false;
    const std::string keepName = r.name();
    r.set(guid, keepName, ip, std::max(lastSeen, r.lastSeen()));
    dirty_ = true;
    return true;
}

GuidDb::MergeResult GuidDb::merge(const GuidDb& other) {
    MergeResult m;
    for (const auto& o : other.recs_) {
        const std::string g = o.guid();
        if (!validGuid(g)) { ++m.skipped; continue; }
        auto it = index_.find(g);
        if (it == index_.end()) {
            recs_.push_back(o);
            index_[g] = recs_.size() - 1;
            ++m.added;
        } else if (o.lastSeen() > recs_[it->second].lastSeen()) {
            recs_[it->second] = o;
            ++m.updated;
        } else {
            ++m.skipped;
        }
    }
    if (m.added || m.updated) dirty_ = true;
    return m;
}

std::vector<size_t> GuidDb::search(const std::string& text, size_t maxResults) const {
    std::vector<size_t> out;
    const std::string q = lower(text);
    if (q.empty()) return out;
    for (size_t i = 0; i < recs_.size() && out.size() < maxResults; ++i) {
        const std::string name = recs_[i].name();
        if (lower(recs_[i].guid()).find(q) != std::string::npos || lower(name).find(q) != std::string::npos ||
            lower(stripColors(name)).find(q) != std::string::npos || recs_[i].ip().find(q) != std::string::npos)
            out.push_back(i);
    }
    return out;
}

double nowTDateTime() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    const long long days = daysFromCivil(lt.tm_year + 1900, (unsigned)lt.tm_mon + 1, (unsigned)lt.tm_mday) - kDelphiEpoch;
    return (double)days + (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0;
}

std::string formatTDateTime(double t) {
    if (!(t > 1.0 && t < 100000.0)) return {};
    const long long whole = (long long)std::floor(t);
    long long secs = std::llround((t - (double)whole) * 86400.0);
    long long dayAdj = whole;
    if (secs >= 86400) { secs -= 86400; ++dayAdj; }
    long long y;
    unsigned m, d;
    civilFromDays(dayAdj + kDelphiEpoch, y, m, d);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04lld. %02u. %02u. %02lld:%02lld:%02lld", y, m, d, secs / 3600, (secs / 60) % 60, secs % 60);
    return buf;
}

std::string ipWithoutPort(const std::string& addr) {
    const size_t c = addr.find(':');
    return c == std::string::npos ? addr : addr.substr(0, c);
}

}  // namespace miniadmin
