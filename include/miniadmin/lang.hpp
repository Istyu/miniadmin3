// Language files (lang\*.lng next to the exe).
//
// Format (UTF-8, with or without BOM):
//   ; comment
//   language.name=Magyar
//   menu.file=Fájl
//   status.kick.progress=Összes játékos kirúgása: %1/%2 …
// One line = one key. In the text \n = line break, \t = tab, \\ = backslash; %1..%9 are substituted values.
// The original program's .ml files (Delphi component names | text) do not fit this UI, hence a new format.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace miniadmin {

struct Language {
    std::string id;                           // the file name without extension (e.g. "Hungarian") – this is what settings.ini stores
    std::map<std::string, std::string> text;  // key -> UTF-8 text

    bool load(const std::string& path);       // false if the file cannot be read
    bool parse(const std::string& content);   // from the file content; false if it contains no key
    const std::string* find(const std::string& key) const;
    std::string name() const;                 // the value of "language.name", or the id
};

// replaces %1..%9 with the args (%% is a % sign); a missing argument becomes empty text.
template <class S>
S formatText(const S& fmt, const std::vector<S>& args) {
    S out;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] == '%' && i + 1 < fmt.size()) {
            const auto c = fmt[i + 1];
            if (c >= '1' && c <= '9') {
                const size_t n = (size_t)(c - '1');
                if (n < args.size()) out += args[n];
                ++i;
                continue;
            }
            if (c == '%') { out += fmt[i]; ++i; continue; }
        }
        out += fmt[i];
    }
    return out;
}

}  // namespace miniadmin
