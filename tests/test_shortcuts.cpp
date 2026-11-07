// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include "miniadmin/shortcuts.hpp"
using namespace miniadmin;

static std::string slurp(const char* p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    // shortcutctrl.cfg (the user's file)
    std::ofstream("t_sc.cfg", std::ios::binary)
        << "[fastrestart]\r\nctrl=1\r\nkey=F\r\n[reload]\r\nctrl=1\r\nkey=R\r\n[change]\r\nctrl=1\r\nkey=M\r\n[master]\r\n"
           "enabled=0\r\n[gametype]\r\nctrl=1\r\nkey=G\r\n[killcod]\r\nctrl=1\r\nkey=K\r\n";
    const std::string orig = slurp("t_sc.cfg");
    ShortcutConfig c;
    assert(loadShortcuts("t_sc.cfg", c));
    assert(!c.enabled && c.keys[(int)Shortcut::MapChange].key == 'M' && c.keys[(int)Shortcut::KillCod].mod == 1);
    assert(c.keys[0].active() && c.keys[0].modifiers() == 0x0002);  // CTRL
    assert(saveShortcuts("t_sc.cfg", c) && slurp("t_sc.cfg") == orig);  // byte-identical writing back
    c.enabled = true;
    c.keys[1].mod = 0;     // ALT
    c.keys[2].key = 0;     // none
    c.keys[3].mod = 4;     // no hotkey
    assert(saveShortcuts("t_sc.cfg", c));
    ShortcutConfig d;
    assert(loadShortcuts("t_sc.cfg", d) && d.enabled && d.keys[1].modifiers() == 0x0001 && !d.keys[2].active() &&
           !d.keys[3].active());
    std::remove("t_sc.cfg");
    ShortcutConfig def;
    assert(!def.enabled && def.keys[4].key == 'K' && !loadShortcuts("nincs.cfg", def));

    // mapaliases.dat: the original's format (Pmapalias2 is the doubled alias)
    std::vector<MapAlias> list = {{"mp_citystreets", "district"}, {"mp_farm", "dow"}};
    assert(saveMapAliases("t_ma.dat", list));
    const std::string bin = slurp("t_ma.dat");
    assert(bin.compare(0, 4, std::string("\x02\0\0\0", 4)) == 0);
    assert(bin.find(std::string("TPF0\x0DTshortmapitem\x00\x09Pmapalias\x06\x08" "district\x0APmapalias2\x06\x10"
                                "ddiissttrriicctt\x04Pmap\x06\x0Emp_citystreets\x00\x00", 74)) == 4);
    std::vector<MapAlias> back;
    assert(loadMapAliases("t_ma.dat", back) && back.size() == 2 && back[1].map == "mp_farm" && back[1].alias == "dow");
    std::remove("t_ma.dat");
    assert(!loadMapAliases("nincs.dat", back));

    assert(mapForAlias(list, " DISTRICT ") == "mp_citystreets" && mapForAlias(list, "mp_farm") == "mp_farm");
    assert(mapForAlias(list, "mp_crash") == "mp_crash" && mapForAlias(list, "ismeretlen").empty());
    assert(cleanGametype("TDM") == "tdm" && cleanGametype("s d").empty() && cleanGametype("").empty());

    if (argc > 1) {  // the real original file: reading and byte-exact writing back
        std::vector<MapAlias> real;
        assert(loadMapAliases(argv[1], real));
        std::printf("mapaliases.dat: %zu elem\n", real.size());
        assert(saveMapAliases("t_real.dat", real));
        assert(slurp("t_real.dat") == slurp(argv[1]));
        std::remove("t_real.dat");
    }
    std::puts("shortcuts ok");
}
