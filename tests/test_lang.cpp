// Checks the language module and the 6 language files.
// Collects the keys from the UI's (src/gui_main.cpp) T("key", ...) / TF("key", ...) calls and the "gtvar.*" keys,
// and checks that each lang\*.lng file has all of them (and the other way round: no extra
// keys), and that the %1..%9 placeholders match the Hungarian file's.
// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include "miniadmin/lang.hpp"
using namespace miniadmin;

static std::string readAll(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::set<char> placeholders(const std::string& s) {
    std::set<char> o;
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if (s[i] == '%' && s[i + 1] >= '1' && s[i + 1] <= '9') o.insert(s[i + 1]);
    return o;
}

static bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + (size_t)n >= s.size()) return false;
        for (int k = 1; k <= n; ++k)
            if (((unsigned char)s[i + k] >> 6) != 2) return false;
        i += (size_t)n + 1;
    }
    return true;
}

int main() {
    // --- format
    Language l;
    assert(l.parse("\xEF\xBB\xBF; megjegyzés\n# ez is\nlanguage.name=Magyar\r\n a.b = x \nsor=egy\\nketto\\\\\\t.\nrossz sor\n=nincs kulcs\n"));
    assert(l.name() == "Magyar");
    assert(l.find("a.b") && *l.find("a.b") == " x ");  // spaces around the key are cut off, the value's are kept
    assert(*l.find("sor") == "egy\nketto\\\t.");
    assert(!l.find("rossz sor") && l.text.size() == 3);
    Language empty;
    assert(!empty.parse("; csak megjegyzés\n") && empty.name().empty());

    assert(formatText<std::string>("%1/%2 kész, 100%%", {"3", "7"}) == "3/7 kész, 100%");
    assert(formatText<std::string>("%2 előbb, %1 utóbb, %3", {"a", "b"}) == "b előbb, a utóbb, ");
    assert(formatText<std::wstring>(L"Pingszint %1", {L"5"}) == L"Pingszint 5");

    // --- the UI's keys
    const std::string src = std::string(MINIADMIN_SOURCE_DIR);
    const std::string gui = readAll(src + "/src/gui_main.cpp");
    assert(!gui.empty());
    std::set<std::string> used;
    const std::regex call(R"re(\bTF?\(\s*"([^"]+)")re"), gtv(R"re("(gtvar\.[a-z_]+)")re");
    for (auto it = std::sregex_iterator(gui.begin(), gui.end(), call); it != std::sregex_iterator(); ++it) used.insert((*it)[1]);
    for (auto it = std::sregex_iterator(gui.begin(), gui.end(), gtv); it != std::sregex_iterator(); ++it) used.insert((*it)[1]);
    used.insert("language.name");
    // the description templates of Weapons / Perks (src/cvardesc.cpp)
    const std::string cd = readAll(src + "/src/cvardesc.cpp");
    assert(!cd.empty());
    const std::regex wpk(R"re("(wp(?:var|cat|item)\.[A-Za-z0-9_]+)")re");
    for (auto it = std::sregex_iterator(cd.begin(), cd.end(), wpk); it != std::sregex_iterator(); ++it) used.insert((*it)[1]);
    std::printf("a felület %zu kulcsot használ\n", used.size());
    assert(used.size() > 150);

    const char* files[] = {"Hungarian", "English", "Deutsch", "French", "Nederlands", "Spanish"};
    Language hu;
    assert(hu.load(src + "/lang/Hungarian.lng"));
    int errors = 0;
    for (const char* f : files) {
        const std::string path = src + "/lang/" + f + ".lng";
        Language x;
        if (!x.load(path)) { std::printf("HIÁNYZIK: %s\n", path.c_str()); ++errors; continue; }
        assert(x.id == f);
        if (!validUtf8(readAll(path))) { std::printf("%s: nem érvényes UTF-8\n", f); ++errors; }
        for (const auto& k : used)
            if (!x.find(k)) { std::printf("%s: hiányzó kulcs: %s\n", f, k.c_str()); ++errors; }
        for (const auto& kv : x.text) {
            if (!used.count(kv.first)) { std::printf("%s: felesleges kulcs: %s\n", f, kv.first.c_str()); ++errors; }
            if (kv.second.empty()) { std::printf("%s: üres szöveg: %s\n", f, kv.first.c_str()); ++errors; }
            const std::string* h = hu.find(kv.first);
            if (h && placeholders(*h) != placeholders(kv.second)) {
                std::printf("%s: eltérő %%-helyőrzők: %s\n", f, kv.first.c_str());
                ++errors;
            }
        }
    }
    if (errors) { std::printf("%d hiba\n", errors); return 1; }
    std::puts("lang ok");
}
