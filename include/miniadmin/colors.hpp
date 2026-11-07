// Quake3/CoD colour codes (^0 .. ^9) for the GUI.
#pragma once
#include <string>
#include <vector>

namespace miniadmin {

struct ColorSegment {
    std::string text;
    int color;  // 0..9
};

// "^1Red^7 normal" -> [{"Red",1},{" normal",7}]. Empty segments are dropped.
// A ^<digit> code is interpreted the same way as in stripColors().
std::vector<ColorSegment> parseColors(const std::string& s, int defaultColor = 7);

// 0xRRGGBB. The palette is tuned for the light grid background (pure
// yellow/cyan/white would be unreadable), so these are not the in-game shades.
unsigned colorRgb(int index);

}  // namespace miniadmin
