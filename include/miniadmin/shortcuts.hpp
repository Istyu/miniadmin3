// Hotkeys (the original "Quick-Commando" window): shortcutctrl.cfg and mapaliases.dat, in the original format.
#pragma once
#include <string>
#include <vector>

namespace miniadmin {

// A hotkey: modifier (index in the original's drop-down list) + one letter / digit.
// mod: 0 = ALT, 1 = CTRL, 2 = SHIFT, 3 = WIN, 4 = no hotkey
struct Hotkey {
    int mod = 1;
    char key = 0;  // 'A'..'Z', '0'..'9'; 0 = not set
    bool active() const { return mod >= 0 && mod <= 3 && key != 0; }
    unsigned modifiers() const;  // the RegisterHotKey value MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN
};

enum class Shortcut { FastRestart = 0, Reload, MapChange, Gametype, KillCod, Count };
constexpr int kShortcutCount = (int)Shortcut::Count;

struct ShortcutConfig {
    bool enabled = false;  // [master] enabled: "Enable hotkeys"
    Hotkey keys[kShortcutCount];
    ShortcutConfig();      // the original's defaults: CTRL+F, R, M, G, K
};

// shortcutctrl.cfg: [fastrestart] [reload] [change] [master] [gametype] [killcod], ctrl= / key= (the original's order)
bool loadShortcuts(const std::string& path, ShortcutConfig& out);
bool saveShortcuts(const std::string& path, const ShortcutConfig& c);

// A map change alias: the typed alias → the map's name.
struct MapAlias {
    std::string map, alias;
};
// mapaliases.dat: Int32 count, then per item a Delphi component stream ("TPF0", Tshortmapitem,
// Pmapalias / Pmapalias2 / Pmap). Pmapalias2 is the alias with every letter doubled (that is how the original's
// keyboard hook saw the typing) – it is saved like this so the original program can read it too.
bool loadMapAliases(const std::string& path, std::vector<MapAlias>& out);
bool saveMapAliases(const std::string& path, const std::vector<MapAlias>& list);

// The map for the typed text: an alias (case-insensitive), or the map name itself. "" = none.
std::string mapForAlias(const std::vector<MapAlias>& list, const std::string& typed);

// The text typed after the gametype hotkey: only letters, digits, underscore (e.g. "tdm", "sd"). "" = invalid.
std::string cleanGametype(const std::string& typed);

}  // namespace miniadmin
