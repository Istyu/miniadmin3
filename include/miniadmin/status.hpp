// Parsing of the getstatus reply.
// The original: 0x512040 (ParseStatusInfo), 0x517464 (server info fields),
// 0x523984 (filling the player grid).
#pragma once
#include <map>
#include <string>
#include <vector>
#include "miniadmin/game.hpp"

namespace miniadmin {

struct Player {
    int slot = -1;     // the line's number in the reply (getstatus gives no slot)
    int score = 0;
    int ping = 0;
    std::string name;  // with colour codes (^1, ^7, ...)
};

struct ServerInfo {
    std::map<std::string, std::string> cvars;  // keys lower-cased
    std::vector<Player> players;
    Game game = Game::Unknown;

    // Case-insensitive lookup; def for a missing key.
    std::string get(const std::string& key, const std::string& def = "") const;

    // Convenience getters for the fields used by the original
    std::string hostname() const { return get("sv_hostname"); }
    std::string mapname() const { return get("mapname"); }
    std::string gametype() const { return get("g_gametype", get("gametype")); }
    int maxClients() const;
    int privateClients() const;  // sv_privateClients (0 if none)
    // The original's Players column: "players/max", with private slots "players/(max-private)(+private)", e.g. "1/34(+2)".
    std::string playerCountText() const;
    int maxPing() const;
    bool passworded() const;     // "pswrd" == "1"
    bool punkbuster() const;     // "sv_punkbuster" == "1"
};

// "^1Name" -> "Name"
std::string stripColors(const std::string& s);

// Parsing of the raw UDP reply ("ÿÿÿÿstatusResponse\n\k\v...\n<score> <ping> \"name\"\n...").
// false if it is not a statusResponse.
bool parseStatusResponse(const std::string& raw, ServerInfo& out);

}  // namespace miniadmin
