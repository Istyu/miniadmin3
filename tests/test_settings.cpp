// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "miniadmin/settings.hpp"
using namespace miniadmin;

int main() {
    Settings d;
    assert(d.adminPrefix == "^9mini^4Admin: ^7");  // the last colour is 7 (white)
    assert(d.exes.size() == 19 && d.refreshMode == RefreshMode::AllServers && d.addAfter == 3);

    Settings s;
    s.adminPrefix = " ^1Stealth: ^7";  // the spaces are kept too
    s.refreshMode = RefreshMode::TwoAtATime;
    s.joinAction = JoinAction::Minimize;
    s.addPosition = AddPosition::AfterIndex;
    s.addAfter = 5;
    s.logCommands = true;
    s.launchParams = "+set fs_game mods/x";
    s.joinCommand = "C:\\Program Files\\TS3\\ts3client.exe";
    for (auto& e : s.exes) if (e.game == "COD4" && e.protocol == "2") e.path = "D:\\CoD4\\iw3mp.exe";
    assert(s.save("t_settings.ini"));

    Settings l;
    assert(l.load("t_settings.ini"));
    assert(l.adminPrefix == " ^1Stealth: ^7" && l.refreshMode == RefreshMode::TwoAtATime);
    assert(l.joinAction == JoinAction::Minimize && l.addPosition == AddPosition::AfterIndex && l.addAfter == 5);
    assert(l.logCommands && l.launchParams == "+set fs_game mods/x" && l.joinCommand == s.joinCommand);

    // path lookup: exact protocol -> game;* -> any row of the game -> none
    assert(l.findExe(Game::COD4, "2") && l.findExe(Game::COD4, "2")->path == "D:\\CoD4\\iw3mp.exe");
    assert(l.findExe(Game::COD4, "21") && l.findExe(Game::COD4, "21")->path == "D:\\CoD4\\iw3mp.exe");  // CoD4x
    assert(l.findExe(Game::COD1, "5") == nullptr);
    assert(l.findExe(Game::Unknown, "2") == nullptr);

    assert(buildLaunchCommandLine("D:\\a b\\iw3mp.exe", Endpoint{"1.2.3.4", 28960}, "+set x 1") ==
           "\"D:\\a b\\iw3mp.exe\" +connect 1.2.3.4:28960 +set x 1");
    assert(buildLaunchCommandLine("x.exe", Endpoint{"h", 1}, "") == "\"x.exe\" +connect h:1");

    // Join server: name, password (if any), before connect; quotes are removed
    assert(buildJoinCommandLine("x.exe", Endpoint{"1.2.3.4", 28960}, "^1A\"b", "titok", "+set fs_game x") ==
           "\"x.exe\" +name \"^1Ab\" +password \"titok\" +connect 1.2.3.4:28960 +set fs_game x");
    assert(buildJoinCommandLine("x.exe", Endpoint{"h", 1}, "N", "", "") == "\"x.exe\" +name \"N\" +connect h:1");
    assert(buildJoinCommandLine("x.exe", Endpoint{"h", 1}, "", "pw", "") == "\"x.exe\" +password \"pw\" +connect h:1");  // without a name

    // history (names.txt / passes.txt)
    std::vector<std::string> h{"a", "b", "c"};
    pushHistory(h, "b");
    assert((h == std::vector<std::string>{"b", "a", "c"}));
    pushHistory(h, "");
    pushHistory(h, "d", 3);
    assert((h == std::vector<std::string>{"d", "b", "a"}));
    {
        const std::string hp = (std::filesystem::temp_directory_path() / "ma3_names.txt").string();
        assert(saveHistory(hp, h));
        assert(loadHistory(hp) == h);
        assert(loadHistory(hp + ".nincs").empty());
    }

    Settings p;
    p.addPosition = AddPosition::Top;        assert(insertIndex(p, 7) == 0);
    p.addPosition = AddPosition::Bottom;     assert(insertIndex(p, 7) == 7);
    p.addPosition = AddPosition::AfterIndex; p.addAfter = 3; assert(insertIndex(p, 7) == 3 && insertIndex(p, 2) == 2);

    // prefix: there must always be a space between the name and the message
    assert(messagePrefix("Stealth") == "Stealth ");
    assert(messagePrefix("Stealth:") == "Stealth: ");
    assert(messagePrefix("^9mini^4Admin: ^7") == "^9mini^4Admin: ^7");  // there is already a space before the colour code
    assert(messagePrefix("^1Stealth^7") == "^1Stealth^7 ");
    assert(messagePrefix("Stealth ") == "Stealth ");
    assert(messagePrefix("") == "" && messagePrefix("^7") == "^7");

    // ping visualisation: defaults (from the original), save/load, level classification, tidying
    Settings pd;
    assert(pd.pingLevels[0] == 20 && pd.pingLevels[4] == 100 && pd.pingMeter && pd.pingMeterColored && pd.pingMeterMax == 100);
    assert(pingLevelIndex(pd, 5) == 0 && pingLevelIndex(pd, 20) == 0 && pingLevelIndex(pd, 21) == 1);
    assert(pingLevelIndex(pd, 100) == 4 && pingLevelIndex(pd, 101) == 5 && pingLevelIndex(pd, 999) == 5);
    Settings ps;
    const int lv[5] = {30, 50, 70, 120, 200};
    for (int i = 0; i < 5; ++i) ps.pingLevels[i] = lv[i];
    ps.pingMeter = false;
    ps.pingMeterColored = false;
    ps.pingMeterMax = 250;
    ps.language = "Deutsch";
    assert(ps.save("t_ping.ini"));
    Settings pl;
    assert(pl.load("t_ping.ini"));
    for (int i = 0; i < 5; ++i) assert(pl.pingLevels[i] == lv[i]);
    assert(!pl.pingMeter && !pl.pingMeterColored && pl.pingMeterMax == 250 && pl.language == "Deutsch");
    std::remove("t_ping.ini");
    Settings bad;
    const int b[5] = {50, 10, 0, 2000, 60};
    for (int i = 0; i < 5; ++i) bad.pingLevels[i] = b[i];
    bad.pingMeterMax = 3;
    normalizePing(bad);
    assert(bad.pingLevels[0] == 50 && bad.pingLevels[1] == 50 && bad.pingLevels[2] == 50 && bad.pingLevels[3] == 999 &&
           bad.pingLevels[4] == 999 && bad.pingMeterMax == 10);

    Settings none;
    assert(!none.load("nincs_ilyen_fajl.ini") && none.adminPrefix == "^9mini^4Admin: ^7");
    std::remove("t_settings.ini");

    // the original config.cfg (ANSI, modelled on the user's file)
    {
        const std::string cp = (std::filesystem::temp_directory_path() / "ma3_config.cfg").string();
        std::ofstream(cp, std::ios::binary)
            << "[Join]\r\nnoop=0\r\nmin=1\r\nquit=0\r\n[refresh]\r\nall=0\r\none=0\r\n2=1\r\n[fade]\r\nfade=0\r\n[hide]\r\ntray=1\r\n"
               "[save]\r\nwindowpos=1\r\n[table]\r\ncolors=0\r\n[style]\r\nvisual=0\r\n[Start]\r\nparam=+set fs_game x\r\ncommand=\r\n"
               "[lineadd]\r\nline=7\r\n[serveradd]\r\n1=0\r\n2=0\r\n3=1\r\n[formpos]\r\nmaximized=0\r\nleft=1139\r\ntop=-5\r\n"
               "[logging]\r\ncmds=1\r\n[LNG]\r\nindex=3\r\n[admin]\r\nnick=Stealth\r\n[pam]\r\nautocheckup=0\r\n"
               "[pinglevel]\r\n1=20\r\n2=45\r\n3=60\r\n4=80\r\n5=100\r\n[ping]\r\nmaxlevel=120\r\n[pingmeter]\r\nenabled=1\r\ncolored=0\r\n"
               "[CodExePath]\r\n2=\r\n1=\r\n5=\r\n115=C:\\Program Files (x86)\\Activision\\Call of Duty 2\\cod2mp_s.exe\r\n3=\r\n"
               "[masterserverlist]\r\nthreadcount=50\r\n[CodExePath4]\r\n2=D:\\CoD4\\iw3mp.exe\r\n6=D:\\CoD4\\iw3mp.exe\r\n"
               "[punkbuster]\r\nkick_ban=1\r\n[ask]\r\ndelsrv=0\r\n[delss]\r\nserverdel=1\r\n[cpu]\r\naffinity=2\r\n"
               "[CodExePath5]\r\n94=E:\\J\xE1t\xE9k\\CoDWaWmp.exe\r\n[ujszekcio]\r\nvalami=1\r\n";
        Settings c;
        assert(c.load(cp));
        assert(c.joinAction == JoinAction::Minimize && c.refreshMode == RefreshMode::TwoAtATime);
        assert(c.addPosition == AddPosition::AfterIndex && c.addAfter == 7 && c.launchParams == "+set fs_game x");
        assert(c.trayMinimize && c.saveWindowPos && !c.coloredNames && c.winLeft == 1139 && c.winTop == -5 && !c.maximized);
        assert(c.logCommands && c.language == "Hungarian" && c.adminPrefix == "Stealth");
        assert(c.pingLevels[1] == 45 && c.pingMeterMax == 120 && c.pingMeter && !c.pingMeterColored);
        assert(!c.askDeleteServer && c.cpuAffinity == 2 && c.masterThreads == 50 && c.pbKickBan);
        assert(c.exes.size() == 19);
        assert(c.findExe(Game::COD2, "115")->path == "C:\\Program Files (x86)\\Activision\\Call of Duty 2\\cod2mp_s.exe");
        assert(c.findExe(Game::COD4, "6")->path == "D:\\CoD4\\iw3mp.exe");
        assert(c.findExe(Game::COD5, "94")->path == "E:\\J\xC3\xA1t\xC3\xA9k\\CoDWaWmp.exe");  // ANSI → UTF-8
        assert(c.findExe(Game::COD1, "5") == nullptr);
        // saving: the same structure, the unknown section is kept too
        const std::string sp = (std::filesystem::temp_directory_path() / "ma3_settings.ini").string();
        assert(c.save(sp));
        std::ifstream f(sp, std::ios::binary);
        const std::string txt((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        assert(txt.find("[Join]\r\nnoop=0\r\nmin=1\r\nquit=0\r\n") == 0);
        assert(txt.find("[LNG]\r\nindex=3\r\nname=Hungarian\r\n") != std::string::npos);
        assert(txt.find("[CodExePath4]\r\n2=D:\\CoD4\\iw3mp.exe\r\n1=\r\n3=\r\n") != std::string::npos);
        assert(txt.find("[ujszekcio]\r\nvalami=1\r\n") != std::string::npos);
        Settings c2;
        assert(c2.load(sp));
        assert(c2.findExe(Game::COD5, "94")->path == c.findExe(Game::COD5, "94")->path && c2.winTop == -5 && c2.cpuAffinity == 2);
        assert(c2.language == "Hungarian" && !c2.coloredNames && c2.extra.size() == c.extra.size());
    }
    // the old (section-less) settings.ini: the path of the "*" row goes into every empty row of the game
    {
        const std::string op = (std::filesystem::temp_directory_path() / "ma3_old.ini").string();
        std::ofstream(op, std::ios::binary) << "language=English\nadminPrefix=^1X\nexe=COD4\t*\t\tD:\\c4.exe\nexe=COD4\t2\t1.3\tD:\\c4_13.exe\n";
        Settings o;
        assert(o.load(op) && o.language == "English" && o.adminPrefix == "^1X" && o.exes.size() == 19);
        assert(o.findExe(Game::COD4, "2")->path == "D:\\c4_13.exe" && o.findExe(Game::COD4, "5")->path == "D:\\c4.exe");
    }
    assert(originalLanguageName(0) == "Deutsch" && originalLanguageName(6).empty() && originalLanguageIndex("spanish") == 5);
    std::puts("settings ok");
}
