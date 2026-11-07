#include "miniadmin/alias.hpp"
#include "miniadmin/status.hpp"  // stripColors

#include <algorithm>
#include <fstream>

namespace miniadmin {

namespace {
constexpr size_t kMaxNamesPerGuid = 30;

std::string cleanName(const std::string& s) {
    std::string o;
    for (char c : s)
        if (c != '\t' && c != '\r' && c != '\n') o += c;
    size_t a = o.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    size_t b = o.find_last_not_of(' ');
    return o.substr(a, b - a + 1);
}
}  // namespace

bool AliasDb::validGuid(const std::string& g) {
    if (g.empty() || g.find_first_of(" \t\r\n") != std::string::npos) return false;
    return g.find_first_not_of('0') != std::string::npos;  // all zeros = no GUID
}

bool AliasDb::add(const std::string& guid, const std::string& name) {
    if (!validGuid(guid)) return false;
    const std::string n = cleanName(name);
    if (n.empty()) return false;
    auto& v = db_[guid];
    if (std::find(v.begin(), v.end(), n) != v.end()) return false;
    v.push_back(n);
    if (v.size() > kMaxNamesPerGuid) v.erase(v.begin());
    return true;
}

std::vector<std::string> AliasDb::aliases(const std::string& guid, const std::string& except) const {
    std::vector<std::string> out;
    auto it = db_.find(guid);
    if (it == db_.end()) return out;
    const std::string ex = cleanName(except);
    for (const auto& n : it->second)
        if (n != ex) out.push_back(n);
    return out;
}

int AliasDb::importGuidsDat(const std::string& path) {
    constexpr size_t kRec = 98;
    std::ifstream in(path, std::ios::binary);
    if (!in) return -1;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0 || (size_t)size % kRec != 0) return -1;
    in.seekg(0);
    auto shortStr = [](const char* p, size_t maxLen, std::string& out) {
        const size_t len = (unsigned char)p[0];
        if (len > maxLen) return false;
        out.assign(p + 1, len);
        return true;
    };
    int added = 0;
    char rec[kRec];
    for (size_t i = 0, n = (size_t)size / kRec; i < n && in.read(rec, kRec); ++i) {
        std::string guid, name;
        if (!shortStr(rec, 32, guid) || !shortStr(rec + 33, 40, name)) continue;
        if (add(guid, stripColors(name))) ++added;
    }
    return added;
}

bool AliasDb::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    db_.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t t = line.find('\t');
        if (t == std::string::npos) continue;
        add(line.substr(0, t), line.substr(t + 1));
    }
    return true;
}

bool AliasDb::save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const auto& kv : db_)
        for (const auto& n : kv.second) out << kv.first << '\t' << n << '\n';
    return (bool)out;
}

}  // namespace miniadmin
