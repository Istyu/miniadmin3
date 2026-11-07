// Tests of the logic behind the GUI (colour codes, rcon status, map list,
// endpoint parsing, pollServer + RconClient with a fake UDP server).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <atomic>

#include "miniadmin/alias.hpp"
#include "miniadmin/colors.hpp"
#include "miniadmin/hostinfo.hpp"
#include <filesystem>
#include <fstream>
// a temporary file in the system temp folder ("/tmp" does not always exist on Windows)
static std::string tmp(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }
#include "miniadmin/monitor.hpp"
#include "miniadmin/rconparse.hpp"

#ifndef _WIN32
#  include <arpa/inet.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

using namespace miniadmin;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static const std::string FF(4, '\xFF');

static void testColors() {
    auto s = parseColors("^1Piros^7 norm^^2al");
    CHECK(s.size() == 3);
    CHECK(s[0].text == "Piros" && s[0].color == 1);
    CHECK(s[1].text == " norm^" && s[1].color == 7);
    CHECK(s[2].text == "al" && s[2].color == 2);
    CHECK(parseColors("").empty());
    CHECK(parseColors("^1^2").empty());
    auto p = parseColors("sima");
    CHECK(p.size() == 1 && p[0].color == 7);
    CHECK(colorRgb(1) != colorRgb(7));
}

static const char* kStatus =
    "map: mp_crash\n"
    "num score ping guid   name            lastmsg address               qport rate\n"
    "--- ----- ---- ------ --------------- ------- --------------------- ----- -----\n"
    "  0     5   45 123456 ^1Nagy ^2Pista^7        0 1.2.3.4:28960         12345 25000\n"
    "  3   -2 CNCT      0 Lassu                  50 5.6.7.8:1234          777 5000\n"
    "  7    0  999 abcdef0123456789abcdef0123456789 ^7x^7    100 bot                       0 0\n";

static void testRconStatus() {
    RconStatus st;
    CHECK(parseRconStatus(kStatus, st));
    CHECK(st.mapname == "mp_crash");
    CHECK(st.players.size() == 3);
    const auto& a = st.players[0];
    CHECK(a.slot == 0 && a.score == 5 && a.ping == 45 && a.guid == "123456");
    CHECK(a.name == "^1Nagy ^2Pista^7");      // space in the name
    CHECK(a.address == "1.2.3.4:28960" && a.qport == 12345 && a.rate == 25000);
    const auto& b = st.players[1];
    CHECK(b.slot == 3 && b.score == -2 && b.ping == 999 && b.name == "Lassu" && b.lastmsg == 50);
    CHECK(st.players[2].guid.size() == 32 && st.players[2].address == "bot");
    CHECK(!parseRconStatus("Bad rconpassword.\n", st));
}

static void testMapList() {
    auto m = parseMapList("maps/mp:\nmp_crash.d3dbsp\nmp_Backlot.D3DBSP\nmp_crash.d3dbsp\n2 files listed\n");
    CHECK(m.size() == 2 && m[0] == "mp_Backlot" && m[1] == "mp_crash");
    auto o = parseMapList("maps/mp/mp_toujane.bsp\r\nmp_carentan.bsp\r\n");
    CHECK(o.size() == 2 && o[0] == "mp_carentan" && o[1] == "mp_toujane");
    CHECK(parseMapList("nothing here").empty());

    // COD4/COD5: from the map picture names (without prefix, in picture order, without nomap)
    auto p = mapsFromPictures({"COD4mp_backlot", "COD2mp_toujane", "COD4nomap", "COD4mp_bloc", "cod4MP_BACKLOT",
                               "COD4", "COD5mp_airfield", "COD4cube"}, "COD4");
    CHECK(p.size() == 3 && p[0] == "mp_backlot" && p[1] == "mp_bloc" && p[2] == "cube");
    CHECK(mapsFromPictures({"COD4mp_backlot"}, "").empty());

    // dir / .cfg (Server variables → Run CFG)
    auto c = parseCfgList("print\nDirectory of / .cfg\n-----------------\nserver.cfg\nDM.cfg\r\nmods/x/war.cfg\nserver.cfg\n"
                          "valami.txt\n3 files listed\n");
    CHECK(c.size() == 3 && c[0] == "DM.cfg" && c[1] == "server.cfg" && c[2] == "war.cfg");
    CHECK(parseCfgList("Bad rconpassword.\n").empty());
}

static void testEndpoint() {
    Endpoint e;
    CHECK(parseEndpoint(" 1.2.3.4:29004 ", e) && e.host == "1.2.3.4" && e.port == 29004);
    CHECK(parseEndpoint("example.com", e) && e.host == "example.com" && e.port == 28960);
    CHECK(!parseEndpoint("", e));
    CHECK(!parseEndpoint("1.2.3.4:0", e));
    CHECK(!parseEndpoint("1.2.3.4:70000", e));
    CHECK(!parseEndpoint("1.2.3.4:abc", e));
    CHECK(!parseEndpoint(":28960", e));
}


static void writeFile(const std::string& path, const std::string& txt) { std::ofstream(path, std::ios::binary) << txt; }

static void testGeo() {
    uint32_t v;
    CHECK(parseIpv4("1.2.3.4", v) && v == 0x01020304u);
    CHECK(parseIpv4("255.255.255.255", v) && v == 0xFFFFFFFFu);
    CHECK(!parseIpv4("1.2.3", v) && !parseIpv4("1.2.3.4.5", v) && !parseIpv4("256.1.1.1", v));
    CHECK(!parseIpv4("a.b.c.d", v) && !parseIpv4("", v) && !parseIpv4("1.2.3.4:5", v) && !parseIpv4("bot", v));
    CHECK(countryNameFromCode("hu") == "Hungary" && countryNameFromCode("GB") == "United Kingdom");
    CHECK(countryNameFromCode("QQ") == "QQ");

    // DB-IP Lite format, with an IPv6 line and a header
    writeFile(tmp("geo_dbip.csv"),
              "ip_start,ip_end,country\n"
              "2001:db8::,2001:db8::ffff,ZZ\n"
              "8.8.8.0,8.8.8.255,US\n"
              "37.221.209.0,37.221.209.255,GB\r\n"
              "87.229.0.0,87.229.255.255,HU\n");
    GeoDb g;
    CHECK(g.loadCsv(tmp("geo_dbip.csv")) && g.size() == 3);
    std::string c, n;
    CHECK(g.lookup("87.229.115.212", c, n) && c == "HU" && n == "Hungary");
    CHECK(g.lookup("37.221.209.130", c, n) && c == "GB" && n == "United Kingdom");
    CHECK(g.lookup("8.8.8.8", c, n) && c == "US");
    CHECK(!g.lookup("8.8.9.1", c, n));      // gap between the ranges
    CHECK(!g.lookup("1.1.1.1", c, n));      // before the first range
    CHECK(!g.lookup("nem-ip", c, n));

    // IP2Location LITE DB1: numeric range + with a name
    writeFile(tmp("geo_ip2l.csv"),
              "\"16777216\",\"16777471\",\"AU\",\"Australia\"\n"
              "\"3758096384\",\"3758096639\",\"-\",\"-\"\n");
    GeoDb g2;
    CHECK(g2.loadCsv(tmp("geo_ip2l.csv")) == false || g2.size() == 1);
    CHECK(g2.lookup("1.0.0.77", c, n) && c == "AU" && n == "Australia");

    CHECK(!GeoDb().loadCsv(tmp("nincs_ilyen_fajl.csv")));

    // lookupHost: resolving works on localhost, without a network too
    HostInfo h = lookupHost("127.0.0.1", &g);
    CHECK(h.ip == "127.0.0.1");
    CHECK(lookupHost("ez.nem.letezik.invalid", nullptr).ip.empty());
}

static void testAlias() {
    AliasDb a;
    CHECK(a.add("123456", "Pista"));
    CHECK(!a.add("123456", "Pista"));           // duplicate
    CHECK(a.add("123456", "  Bela\t"));         // tab/space cleaned up
    CHECK(!a.add("0", "x") && !a.add("", "x") && !a.add("000", "x") && !a.add("12 3", "x"));
    CHECK(!a.add("123456", "   "));
    auto v = a.aliases("123456", "Pista");
    CHECK(v.size() == 1 && v[0] == "Bela");
    CHECK(a.aliases("999").empty());
    CHECK(a.save(tmp("alias_t.db")));
    AliasDb b;
    CHECK(b.load(tmp("alias_t.db")) && b.aliases("123456").size() == 2);
    CHECK(!b.load(tmp("nincs_alias.db")));
    for (int i = 0; i < 40; ++i) b.add("777", "n" + std::to_string(i));
    CHECK(b.aliases("777").size() == 30 && b.aliases("777").front() == "n10");
}


// ---- synthetic copies of the original Delphi files
static void putLE(std::string& b, unsigned long long v, int n) { for (int i = 0; i < n; ++i) b += (char)((v >> (8 * i)) & 255); }
static void putShort(std::string& b, const std::string& s, size_t cap) {
    b += (char)s.size(); b += s; b.append(cap - s.size(), '\0');
}
static std::string rangeRec(unsigned long long from, unsigned long long to, const std::string& code, const std::string& name) {
    std::string b; putLE(b, from, 8); putLE(b, to, 8); putShort(b, code, 2); putShort(b, name, 52); return b;
}
static std::string guidRec(const std::string& guid, const std::string& name, const std::string& ip) {
    std::string b; putShort(b, guid, 32); putShort(b, name, 40); putShort(b, ip, 15); putLE(b, 0x40e559745a03be7aULL, 8); return b;
}

static void testOriginalFiles() {
    // ipranges.dat
    std::string d = rangeRec(0x0206be38, 0x0206be3f, "GB", "United kingdom") +
                    rangeRec(0x57e50000, 0x57e57fff, "HU", "Hungary") +       // 87.229.0.0-87.229.127.255
                    rangeRec(0x2c000000, 0x2dffffff, "US", "United states");  // 44.0.0.0-45.255.255.255
    writeFile(tmp("ipr.dat"), d);
    GeoDb g;
    CHECK(g.loadIpRangesDat(tmp("ipr.dat")) && g.size() == 3);
    std::string c, n;
    CHECK(g.lookup("2.6.190.60", c, n) && c == "GB" && n == "United kingdom");
    CHECK(g.lookup("87.229.115.212", c, n) && c == "HU" && n == "Hungary");
    CHECK(!g.lookup("37.221.209.130", c, n));
    // geoip.csv takes precedence, the missing ranges come from ipranges.dat
    writeFile(tmp("geo_over.csv"), "45.131.0.0,45.131.255.255,RO\n");
    CHECK(g.loadCsv(tmp("geo_over.csv")));
    CHECK(g.lookup("45.131.148.155", c, n) && c == "RO");                      // the newer csv overrides the old US range
    CHECK(g.lookup("45.1.1.1", c, n) && c == "US" && n == "United states");   // from the dat, with the original name
    CHECK(g.lookup("87.229.115.212", c, n) && c == "HU");
    writeFile(tmp("ipr_bad.dat"), d + "xx");                                  // not a multiple of 72
    CHECK(!GeoDb().loadIpRangesDat(tmp("ipr_bad.dat")));
    CHECK(!GeoDb().loadIpRangesDat(tmp("nincs_ipr.dat")));

    // guids.dat
    std::string gd = guidRec("950079", "^3ki^5ng", "91.147.254.179") +
                     guidRec("2310346616538419195", "|FF|danny", "85.66.97.66") +
                     guidRec("950079", "ki^1ng2", "1.2.3.4");
    writeFile(tmp("g.dat"), gd);
    AliasDb a;
    CHECK(a.importGuidsDat(tmp("g.dat")) == 3);
    auto v = a.aliases("950079");
    CHECK(v.size() == 2 && v[0] == "king" && v[1] == "king2");               // without colour codes
    CHECK(a.importGuidsDat(tmp("g.dat")) == 0);
    writeFile(tmp("g_bad.dat"), gd + "x");
    CHECK(a.importGuidsDat(tmp("g_bad.dat")) == -1);
    CHECK(a.importGuidsDat(tmp("nincs_g.dat")) == -1);
}

static void testRconStatusVariants() {
    // CoD4x: an extra id column between guid and name + a negative port
    RconStatus st;
    CHECK(parseRconStatus(
        "map: mp_crash\n"
        "num score ping guid steamid name lastmsg address qport rate\n"
        "--- ----- ---- ---- ------- ---- ------- ------- ----- ----\n"
        "  3    15  135 2310346616797284002 76561198000000001 ^1Nagy Pista^7      0 84.185.183.210:-1482 3717 25000\n"
        "  5     5   26 0 0 Bot^7 0 bot 0 0\n", st));
    CHECK(st.players.size() == 2);
    CHECK(st.players[0].guid == "2310346616797284002" && st.players[0].name == "^1Nagy Pista^7");
    CHECK(st.players[0].address == "84.185.183.210:-1482" && st.players[0].rate == 25000);
    CHECK(st.players[1].name == "Bot^7" && st.players[1].address == "bot");
    // without a header / with the standard header the behaviour does not change
    CHECK(parseRconStatus("  0  1  2 99 Valaki^7 0 1.2.3.4:5 6 7\n", st) && st.players.size() == 1 && st.players[0].name == "Valaki^7");
}

#ifndef _WIN32
// Fake server: getstatus -> statusResponse, rcon "pw" status -> print + status text
static void testNetwork() {
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    CHECK(::bind(s, (sockaddr*)&a, sizeof a) == 0);
    socklen_t al = sizeof a; ::getsockname(s, (sockaddr*)&a, &al);
    const uint16_t port = ntohs(a.sin_port);

    std::atomic<bool> stop{false};
    std::thread srv([&] {
        timeval tv{0, 100000}; ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char buf[2048];
        while (!stop) {
            sockaddr_in from{}; socklen_t fl = sizeof from;
            int n = (int)::recvfrom(s, buf, sizeof buf, 0, (sockaddr*)&from, &fl);
            if (n <= 4) continue;
            std::string p(buf + 4, n - 4), r;
            if (p.compare(0, 9, "getstatus") == 0)
                r = FF + "statusResponse\n\\sv_hostname\\^1Teszt ^7Szerver\\mapname\\mp_crash\\gamename\\Call of Duty 4\\sv_maxclients\\10\n"
                    "10 45 \"^2Anna\"\n0 80 \"Bela\"\n";
            else if (p == "rcon \"titok\" status")
                r = FF + "print\n" + kStatus;
            else if (p.compare(0, 5, "rcon ") == 0)
                r = FF + "print\nBad rconpassword.\n";
            if (!r.empty()) ::sendto(s, r.data(), r.size(), 0, (sockaddr*)&from, fl);
        }
    });

    Endpoint ep{"127.0.0.1", port};
    Snapshot snap = pollServer(ep, 1000);
    CHECK(snap.online);
    CHECK(snap.pingMs >= 0 && snap.pingMs < 500);
    CHECK(snap.info.hostname() == "^1Teszt ^7Szerver");
    CHECK(snap.info.game == Game::COD4);
    CHECK(snap.info.players.size() == 2 && snap.info.players[0].score == 10);

    RconClient rc(ep, "titok");
    std::string out = rc.execute("status");
    RconStatus st;
    CHECK(parseRconStatus(out, st) && st.players.size() == 3 && st.mapname == "mp_crash");

    // non-existent server: offline, does not hang
    Snapshot dead = pollServer(Endpoint{"127.0.0.1", 1}, 300);
    CHECK(!dead.online && dead.pingMs == 999);

    stop = true; srv.join(); ::close(s);
}
#endif

int main() {
    testColors();
    testRconStatus();
    testMapList();
    testEndpoint();
    testGeo();
    testAlias();
    testOriginalFiles();
    testRconStatusVariants();
#ifndef _WIN32
    testNetwork();
#endif
    std::printf(g_fail ? "%d HIBA\n" : "minden teszt OK\n", g_fail);
    return g_fail ? 1 : 0;
}
