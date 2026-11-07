// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "miniadmin/master.hpp"
using namespace miniadmin;

static std::string addr(unsigned char a, unsigned char b, unsigned char c, unsigned char d, unsigned port) {
    std::string s = "\\";
    s += (char)a; s += (char)b; s += (char)c; s += (char)d;
    s += (char)(port >> 8); s += (char)(port & 255);
    return s;
}

int main() {
    // masterservers.txt (modelled on the user's file)
    auto list = parseMasterList(
        "c46:63.146.124.21:20810\r\nc42:63.146.124.21:20810\r\n118:cod2master.activision.com:20710\r\n"
        "22:codmaster.activision.com:20610\r\n6:63.146.124.59:20510\r\ncod4master.cod4x.me\r\n"
        "cod4master.doszgep.cloud:20811\r\n\r\n; megjegyzés\r\nrossz:sor:abc\r\n");
    assert(list.size() == 7);
    assert(list[0].key == "c46" && list[0].ep.host == "63.146.124.21" && list[0].ep.port == 20810);
    assert(list[5].key.empty() && list[5].ep.host == "cod4master.cod4x.me" && !list[5].hasPort);
    assert(list[6].key.empty() && list[6].ep.port == 20811 && list[6].hasPort);

    const auto& vs = masterVersions();
    assert(vs.size() == 10 && std::string(vs[0].label) == "COD4 1.7" && vs[0].protocol == 6 &&
           std::string(vs[9].label) == "COD1 1.5");
    auto m = mastersFor(list, vs[0]);  // COD4 1.7: the c46 line + the key-less ones (default port: 20810)
    assert(m.size() == 4 && m[0].host == "63.146.124.21" && m[1].host == "cod4master.cod4x.me" && m[1].port == 20810 &&
           m[2].port == 20811 && m[3].host == "cod4master.activision.com");  // + the official one, by name
    auto m2 = mastersFor(list, vs[5]);  // COD2 1.3
    assert(m2.size() == 3 && m2[0].host == "cod2master.activision.com" && m2[1].port == 20710);  // the official one is already in it

    assert(getServersPacket(6) == std::string(4, '\xFF') + "getservers 6 empty full");

    // getserversResponse: \ + 4 bytes IP + 2 bytes port; a middle packet ends with \EOT, the last one with \EOF
    std::string r = std::string(4, '\xFF') + "getserversResponse\n" + addr(1, 2, 3, 4, 28960) +
                    addr(92, 92, 92, 92, 0x5C5C) +  // the address bytes may contain '\' too
                    addr(0, 0, 0, 0, 0) + "\\EOF" + std::string(3, '\0');
    std::vector<Endpoint> eps;
    assert(parseGetServersResponse(r, eps) == MasterEnd::List);
    assert(eps.size() == 2 && eps[0].host == "1.2.3.4" && eps[0].port == 28960 && eps[1].host == "92.92.92.92" &&
           eps[1].port == 0x5C5C);
    eps.clear();
    const std::string oob(4, '\xFF');
    assert(parseGetServersResponse(oob + "getserversResponse" + addr(10, 0, 0, 1, 1) + "\\EOT", eps) == MasterEnd::Packet &&
           eps.size() == 1);  // more packets to come
    assert(parseGetServersResponse(oob + "getserversResponse" + addr(10, 0, 0, 2, 1), eps) == MasterEnd::None && eps.size() == 2);
    assert(parseGetServersResponse("valami", eps) == MasterEnd::None);

    // infoResponse
    BrowserServer s;
    assert(parseInfoResponse(std::string(4, '\xFF') + "infoResponse\n\\challenge\\xxx\\protocol\\6\\hostname\\^1SAS ^7Squad"
                             "\\mapname\\mp_crash\\clients\\12\\sv_maxclients\\32\\gametype\\war\\pure\\1\\kc\\1\\hw\\1"
                             "\\mod\\0\\voice\\1\\pb\\0\\pswrd\\1\\maxPing\\350\n",
                             s));
    assert(s.hostname == "^1SAS ^7Squad" && s.mapname == "mp_crash" && s.gametype == "war" && s.clients == 12 &&
           s.maxClients == 32 && s.pure == 1 && s.mod == 0 && s.voice == 1 && s.password == 1 && s.maxPing == 350 &&
           s.protocol == 6);
    BrowserServer s2;
    assert(parseInfoResponse(std::string(4, '\xFF') + "infoResponse\n\\hostname\\x\\fs_game\\mods/abc", s2) && s2.mod == 1 &&
           s2.maxPing == -1 && s2.password == -1);
    assert(!parseInfoResponse(std::string(4, '\xFF') + "statusResponse\n\\a\\b", s2));

    // filter
    BrowserFilter f;
    assert(matchFilter(s, f));
    f.useMap = true; f.map = "CRASH";
    assert(matchFilter(s, f));
    f.map = "backlot";
    assert(!matchFilter(s, f));
    f = BrowserFilter{};
    f.usePassword = true; f.password = false;  // servers without a password
    assert(!matchFilter(s, f));
    f = BrowserFilter{};
    f.useMaxPing = true; f.maxPingLess = false; f.maxPingValue = 300;  // maxPing > 300
    assert(matchFilter(s, f));
    f.maxPingLess = true;
    assert(!matchFilter(s, f));
    f = BrowserFilter{};
    f.usePlayers = true; f.playersLess = true; f.playersValue = 12;  // < 12 players
    assert(!matchFilter(s, f));
    f = BrowserFilter{};
    f.notEmptyNotFull = true;
    assert(matchFilter(s, f));
    BrowserServer full = s;
    full.clients = 32;
    assert(!matchFilter(full, f));
    f = BrowserFilter{};
    f.useCountry = true; f.country = "hu, at";
    s.country = "AT";
    assert(matchFilter(s, f));
    s.country = "DE";
    assert(!matchFilter(s, f));

    // sorting
    BrowserServer a = s, b = s;
    a.hostname = "^1alfa"; b.hostname = "Beta";
    assert(browserLess(a, b, BrowserCol::Name) && !browserLess(b, a, BrowserCol::Name));
    a.hostname = " ^1 zeta"; b.hostname = "Beta";  // surrounding spaces do not matter
    assert(browserLess(b, a, BrowserCol::Name));
    a.clients = 5; b.clients = 3;
    assert(browserLess(b, a, BrowserCol::Players));
    a.ep = {"10.0.0.2", 1}; b.ep = {"9.0.0.1", 1};
    assert(browserLess(b, a, BrowserCol::Address));  // as numbers, not as text
    std::puts("master ok");
}
