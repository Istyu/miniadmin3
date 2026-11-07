#include "miniadmin/lang.hpp"

#include <fstream>
#include <iterator>

namespace miniadmin {

static std::string unescape(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            const char c = v[i + 1];
            if (c == 'n') { o += '\n'; ++i; continue; }
            if (c == 't') { o += '\t'; ++i; continue; }
            if (c == '\\') { o += '\\'; ++i; continue; }
        }
        o += v[i];
    }
    return o;
}

bool Language::parse(const std::string& content) {
    size_t pos = 0;
    if (content.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;  // UTF-8 BOM
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == std::string::npos) end = content.size();
        std::string line = content.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == ';' || line[first] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(first, eq - first);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        if (key.empty()) continue;
        text[key] = unescape(line.substr(eq + 1));  // the value's spaces are kept
    }
    return !text.empty();
}

bool Language::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    text.clear();
    const size_t sl = path.find_last_of("\\/");
    std::string base = sl == std::string::npos ? path : path.substr(sl + 1);
    const size_t dot = base.rfind('.');
    id = dot == std::string::npos ? base : base.substr(0, dot);
    return parse(content);
}

const std::string* Language::find(const std::string& key) const {
    auto it = text.find(key);
    return it == text.end() ? nullptr : &it->second;
}

std::string Language::name() const {
    const std::string* n = find("language.name");
    return n && !n->empty() ? *n : id;
}

}  // namespace miniadmin
