// Parsing of the rcon "status" and "dir maps/mp ..." replies.
//
// NOTE: these formats are based on the standard Quake3/CoD output
// (num score ping guid name lastmsg address qport rate). The original Delphi
// program's routine for this (around 0x5154F0 / 0x515BE4) was not followed
// all the way, so the behaviour was derived from the standard format, not
// from the original.
#pragma once
#include <string>
#include <vector>

namespace miniadmin {

struct RconPlayer {
    int slot = -1;
    int score = 0;
    int ping = 0;        // 999 for CNCT / ZMBI
    std::string guid;
    std::string name;    // with colour codes
    int lastmsg = 0;
    std::string address; // ip:port
    int qport = 0;
    int rate = 0;
};

struct RconStatus {
    std::string mapname;
    std::vector<RconPlayer> players;
};

// false if it found not a single player line and no "map:" line (e.g. an error message).
bool parseRconStatus(const std::string& text, RconStatus& out);

// Map names from the output of "dir maps/mp .bsp" / "... d3dbsp" (without extension,
// sorted, without duplicates).
std::vector<std::string> parseMapList(const std::string& dirOutput);

// The server's cfg files from the output of "dir / .cfg" (with extension, e.g. "server.cfg"), alphabetical,
// without duplicates (Server variables tab → Run CFG).
std::vector<std::string> parseCfgList(const std::string& dirOutput);

// The original COD4/COD5 map list: of the mappic picture names (without extension, e.g. "COD4mp_backlot")
// those starting with the game prefix (gameTag, e.g. "COD4"), without the prefix, in picture order.
// The "nomap" picture and duplicates (case-insensitive) are skipped.
std::vector<std::string> mapsFromPictures(const std::vector<std::string>& pictures, const std::string& gameTag);

}  // namespace miniadmin
