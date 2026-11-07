#include "miniadmin/colors.hpp"
#include <cctype>

namespace miniadmin {

std::vector<ColorSegment> parseColors(const std::string& s, int defaultColor) {
    std::vector<ColorSegment> out;
    std::string cur;
    int color = defaultColor;
    auto flush = [&] {
        if (!cur.empty()) { out.push_back({cur, color}); cur.clear(); }
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '^' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1])) {
            flush();
            color = s[i + 1] - '0';
            ++i;
        } else {
            cur += s[i];
        }
    }
    flush();
    return out;
}

unsigned colorRgb(int index) {
    static const unsigned pal[10] = {
        0x000000,  // ^0 black
        0xD00000,  // ^1 red
        0x008000,  // ^2 green
        0xB08000,  // ^3 yellow (darkened)
        0x0000D0,  // ^4 blue
        0x008B8B,  // ^5 cyan (darkened)
        0xC000C0,  // ^6 magenta
        0x303030,  // ^7 white -> dark text (on a light background)
        0x806000,  // ^8
        0x808080,  // ^9 grey
    };
    return (index >= 0 && index <= 9) ? pal[index] : pal[7];
}

}  // namespace miniadmin
