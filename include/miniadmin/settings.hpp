// Program settings (the data of the "File -> Settings" window) and the "Join server" launch command line.
// Storage: settings.ini next to the exe, with the structure of the original config.cfg ([Join], [refresh], [CodExePath4] …),
// so the original config.cfg can be read too. The old (section-less, "key=value") settings.ini is loaded as well.
#pragma once
#include <string>
#include <vector>
#include "miniadmin/game.hpp"
#include "miniadmin/net.hpp"

namespace miniadmin {

// A row of the "Call of Duty install paths" table.
struct GameExe {
    std::string game;      // COD1 / CODUO / COD2 / COD4 / COD5  (gameTag)
    std::string protocol;  // the server's "protocol" value, or "*" = any
    std::string version;   // an informational label only (e.g. "1.3")
    std::string path;      // the game's .exe; empty = not set
};

enum class JoinAction { KeepOpen = 0, Minimize = 1, Close = 2 };           // what the program does when joining a server
enum class RefreshMode { AllServers = 0, SelectedOnly = 1, TwoAtATime = 2 };  // automatic server refresh
enum class AddPosition { Top = 0, Bottom = 1, AfterIndex = 2 };            // position of a new server in the list

struct Settings {
    // The prefix of admin messages (say / tell). The "Your nickname" field edits this.
    std::string adminPrefix = "^9mini^4Admin: ^7";
    JoinAction joinAction = JoinAction::KeepOpen;
    RefreshMode refreshMode = RefreshMode::AllServers;
    std::vector<GameExe> exes = defaultExes();
    std::string launchParams;   // extra launch parameters for the game
    std::string joinCommand;    // command/program started when joining a server
    AddPosition addPosition = AddPosition::Top;
    int addAfter = 3;           // for AfterIndex: inserted AFTER this many items
    bool logCommands = false;   // [logging] cmds: logging rcon commands to cmdlog.txt
    std::string language = "Hungarian";  // name of the lang\<language>.lng file

    // Ping visualisation (the original "Ping Settings" window). The levels in ms, ascending.
    int pingLevels[5] = {20, 40, 60, 80, 100};  // Ping colouring: ping level 1..5
    bool pingMeter = true;          // Ping Meter: the graph is shown in the ping column
    bool pingMeterColored = true;   // the graph bars in the ping level's colour (otherwise single-coloured)
    int pingMeterMax = 100;         // maximum level: the bar is full at this many ms

    // main window position ([save] windowpos, [formpos]); left/top < -30000 = not saved
    bool saveWindowPos = true;
    bool maximized = false;
    int winLeft = -32768, winTop = -32768, winWidth = 0, winHeight = 0;
    bool coloredNames = true;       // [table] colors: coloured player/server names
    bool askDeleteServer = true;    // [ask] delsrv: ask before deleting a server

    // Further settings of the original: the program reads and writes them back, but does not use them (yet).
    bool fadeIn = false;            // [fade] fade: window fade-in
    bool trayMinimize = false;      // [hide] tray: minimise to the notification area
    bool ownVisualStyle = false;    // [style] visual
    bool pamAutoCheck = false;      // [pam] autocheckup
    bool pbKickBan = true;          // [punkbuster] kick_ban
    bool deleteSsOnServer = true;   // [delss] serverdel
    int cpuAffinity = 0;            // [cpu] affinity: 0 = all, 1 = CPU 1, 2 = CPU 2, 3 = CPU 1+2
    int masterThreads = 50;         // [masterserverlist] threadcount: 4 times this many getinfo requests at a time
    std::string masterVersion = "COD4 1.7";  // [masterserverlist] version: the last chosen CoD version (new key)

    // Unknown sections / keys: written back unchanged.
    struct Extra { std::string section, key, value; };
    std::vector<Extra> extra;

    static std::vector<GameExe> defaultExes();

    // false if it cannot be read (the defaults stay then). Non-UTF-8 text (the original's ANSI
    // config.cfg) is treated as code page 1252 and converted to UTF-8.
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Exact (game + protocol) match, then "game;*", finally any configured row of the game.
    // nullptr if there is no path for this game.
    const GameExe* findExe(Game g, const std::string& protocol) const;
};

// The original's [LNG] index: 0 Deutsch, 1 English, 2 French, 3 Hungarian, 4 Nederlands, 5 Spanish.
// Empty / -1 if there is no such.
std::string originalLanguageName(int index);
int originalLanguageIndex(const std::string& name);

// "\"C:\\...\\iw3mp.exe\" +connect host:port [extra]"
std::string buildLaunchCommandLine(const std::string& exePath, const Endpoint& ep, const std::string& extraParams);

// The "Join server" window's command line: "\"exe\" [+name \"nick\"] [+password \"pw\"] +connect host:port [extra]".
// The name and the password come BEFORE connect (after it in the original), so the connection already uses them.
// Quotes are removed from the name / password (they would break the command line). Empty name / password: no
// +name / +password (the game joins with its own configured name).
std::string buildJoinCommandLine(const std::string& exePath, const Endpoint& ep, const std::string& name,
                                 const std::string& password, const std::string& extraParams);

// History list (the original names.txt / passes.txt: one item per line). A new item goes to the front, an
// earlier identical item is removed, at most maxItems remain. Empty items are not added.
void pushHistory(std::vector<std::string>& list, const std::string& item, size_t maxItems = 20);
std::vector<std::string> loadHistory(const std::string& path);
bool saveHistory(const std::string& path, const std::vector<std::string>& list);

// The prefix put before a message: if the last visible character (not counting trailing ^0..^9 colour codes) is not
// a space, a space is appended, so the name and the message text do not run together ("Stealth" + "Hello").
std::string messagePrefix(const std::string& prefix);

// Insert index (0..count) of a new server according to the setting.
int insertIndex(const Settings& s, int count);

// Which ping level the ping falls into: 0..4 = ping level 1..5 (ping <= the level's value), 5 = above level 5.
int pingLevelIndex(const Settings& s, int pingMs);

// Tidies the levels: between 1..999 ms, and each at least as large as the previous one. The maximum level is 10..999.
void normalizePing(Settings& s);

}  // namespace miniadmin
