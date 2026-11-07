// Master server list (the original "Masterserverliste" window): masterservers.txt, the master servers'
// getservers replies, the servers' getinfo replies, filtering and sorting.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "miniadmin/game.hpp"
#include "miniadmin/net.hpp"

namespace miniadmin {

// A line of masterservers.txt: "key:host:port" (e.g. "c46:63.146.124.21:20810", "118:cod2master...:20710").
// The key is the CoD version (c4x = COD4 protocol x, the others are the protocol number). A line without a key
// ("host" or "host:port") is a master server usable for every version.
struct MasterEntry {
    std::string key;
    Endpoint ep;
    bool hasPort = false;  // if missing, the version's default port is used
};

// One item of the "COD version" list (the original's 10 items, in the same order).
struct MasterVersion {
    const char* label;   // "COD4 1.7"
    const char* key;     // "c46"
    Game game;
    int protocol;        // protocol number of the getservers request
    uint16_t port;       // default port of the master server
};
const std::vector<MasterVersion>& masterVersions();

std::vector<MasterEntry> parseMasterList(const std::string& text);
// The version's master servers: the lines with the exact key, then the key-less ones (with the default port if none given).
// Finally the game's official master server by name (the old IP addresses in masterservers.txt no longer answer).
std::vector<Endpoint> mastersFor(const std::vector<MasterEntry>& list, const MasterVersion& v);

// "ÿÿÿÿgetservers <protocol> empty full" (as in the original)
std::string getServersPacket(int protocol);
// Appends the addresses of one getserversResponse packet to out. The master sends several packets: the middle ones
// end with \EOT, the last one with \EOF (the original waits for \EOF too).
enum class MasterEnd { None, Packet, List };
MasterEnd parseGetServersResponse(const std::string& packet, std::vector<Endpoint>& out);

// A server in the list (getinfo). -1 = the server did not send it.
struct BrowserServer {
    Endpoint ep;
    int pingMs = 999;
    std::string hostname, mapname, gametype;  // the server's text (1252, with colour codes)
    int clients = 0, maxClients = 0, maxPing = -1, protocol = 0;
    int pure = -1, mod = -1, voice = -1, password = -1;
    std::string country;  // two-letter code (filled in by the GUI), upper case
};
bool parseInfoResponse(const std::string& packet, BrowserServer& s);

// The original's Filter tab. A condition only counts when it is switched on.
struct BrowserFilter {
    bool useMap = false;       std::string map;        // in the map name (case-insensitive)
    bool useGametype = false;  std::string gametype;
    bool usePure = false;      bool pure = true;       // "Pure" + "Active": sv_pure 1 / 0
    bool usePassword = false;  bool password = true;
    bool useMod = false;       bool mod = true;
    bool useVoice = false;     bool voice = true;
    bool useMaxPing = false;   bool maxPingLess = true;   int maxPingValue = 0;    // the server's maxPing < / > value
    bool useMaxClients = false; bool maxClientsLess = true; int maxClientsValue = 0;
    bool usePlayers = false;   bool playersLess = true;   int playersValue = 0;
    bool notEmptyNotFull = false;
    bool useCountry = false;   std::string country;    // e.g. "HU" (several too: "HU,AT")
};
bool matchFilter(const BrowserServer& s, const BrowserFilter& f);

// Columns (in table order) and sorting.
enum class BrowserCol { Address = 0, Ping, Name, Map, Players, MaxPing, Mod, Pure, Password, Count };
// a < b by the column (for equal values the address decides, so the order is stable)
bool browserLess(const BrowserServer& a, const BrowserServer& b, BrowserCol col);

// ---- queries (call from a worker thread; cancel stops them at any time)
// The master servers' address list. anyAnswer: whether at least one master server answered.
std::vector<Endpoint> queryMasters(const std::vector<Endpoint>& masters, int protocol, int timeoutMs,
                                   const std::atomic<bool>* cancel, bool* anyAnswer);
// getinfo to every server, at most maxInFlight requests at a time (the original's "threadcount"). The responders
// arrive via onServer; onProgress(done, total) now and then.
void queryInfos(const std::vector<Endpoint>& servers, int maxInFlight, int timeoutMs, const std::atomic<bool>* cancel,
                const std::function<void(const BrowserServer&)>& onServer,
                const std::function<void(int, int)>& onProgress);

}  // namespace miniadmin
