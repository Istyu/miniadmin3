// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "miniadmin/commands.hpp"
#include "miniadmin/config.hpp"
#include "miniadmin/ftpclient.hpp"
#include "miniadmin/game.hpp"
#include "miniadmin/net.hpp"
#include "miniadmin/status.hpp"

using namespace miniadmin;

static void testStatus() {
    const std::string raw =
        std::string(4, '\xFF') + "statusResponse\n"
        "\\g_gametype\\war\\gamename\\Call of Duty 4\\mapname\\mp_crash"
        "\\sv_hostname\\^1My ^7Server\\sv_maxclients\\18\\pswrd\\1\\sv_punkbuster\\1"
        "\\sv_maxPing\\250\n"
        "12 45 \"^2Alice\"\n"
        "3 120 \"Bob \"The Man\"\"\n";

    ServerInfo info;
    assert(parseStatusResponse(raw, info));
    assert(info.game == Game::COD4);
    assert(info.mapname() == "mp_crash");
    assert(info.gametype() == "war");
    assert(info.maxClients() == 18);
    assert(info.privateClients() == 0 && info.playerCountText() == "2/18");
    {
        ServerInfo p = info;
        p.cvars["sv_privateclients"] = "2";
        assert(p.privateClients() == 2 && p.playerCountText() == "2/16(+2)");  // the original form
        p.cvars["sv_privateclients"] = "18";
        assert(p.playerCountText() == "2/18");  // meaningless value: the old form stays
    }
    assert(info.maxPing() == 250);
    assert(info.passworded());
    assert(info.punkbuster());
    assert(stripColors(info.hostname()) == "My Server");
    assert(info.players.size() == 2);
    assert(info.players[0].slot == 0 && info.players[0].score == 12 &&
           info.players[0].ping == 45 && stripColors(info.players[0].name) == "Alice");
    assert(info.players[1].name == "Bob \"The Man\"");

    ServerInfo bad;
    assert(!parseStatusResponse("garbage", bad));
}

static void testRconPacket() {
    RconClient rc(Endpoint{"127.0.0.1", 28960}, "secret");
    const std::string p = rc.packet("status");
    assert(p == std::string(4, '\xFF') + "rcon \"secret\" status");
    assert(rc.describe("status", true) == "rcon \"hidden\" status");
    // cmdlog.txt: the original's line (here "hidden" instead of the password)
    assert(cmdLogLine("2026. 11. 07. 13:52:13", Endpoint{"37.221.209.130", 29010},
                      rc.describe("dir maps/mp d3dbsp", true)) ==
           "2026. 11. 07. 13:52:13 37.221.209.130:29010 -> rcon \"hidden\" dir maps/mp d3dbsp\r\n");  // (without the 0xFF prefix)
}

static void testCommands() {
    assert(cmd::say("hi \"x\"") == "say \"hi 'x'\"");
    assert(cmd::clientkick(7) == "clientkick 7");
    assert(cmd::tell(3, "yo") == "tell 3 \"yo\"");
    assert(cmd::pbKick("5", "admin", "HIGHPINGER") ==
           "PB_SV_kick \"5\" 0 \"^7kicked by admin ^7reason: HIGHPINGER\"");
    assert(cmd::sayAs("Bela", "hello") == "say \"Bela: ^7hello\"");
    assert(cmd::pbBan("4", "admin", "cheat") ==
           "PB_SV_Ban \"4\" \"^7banned by admin ^7reason: cheat\"");
    assert(cmd::dirMaps(true) == "dir maps/mp d3dbsp");
    assert(cmd::dirMaps(false) == "dir maps/mp .bsp");
}

static void testGame() {
    assert(gameFromGamename("Call of Duty") == Game::COD1);
    assert(gameFromGamename("CoD:United Offensive") == Game::CODUO);
    assert(gameFromGamename("Call of Duty 2") == Game::COD2);
    assert(gameFromGamename("Call of Duty: World at War") == Game::COD5);
    assert(gameFromTag("coduo") == Game::CODUO);
    assert(std::string(gameTag(Game::COD4)) == "COD4");
}

static void testConfig() {
    ServerList a;
    ServerEntry e;
    e.name = "teszt";
    e.endpoint = {"10.0.0.5", 28961};
    e.rconPassword = "pw";
    e.game = Game::COD2;
    a.entries().push_back(e);
    assert(a.save("test_servers.ini"));

    ServerList b;
    assert(b.load("test_servers.ini"));
    assert(b.entries().size() == 1);
    const ServerEntry* f = b.find("teszt");
    assert(f && f->endpoint.host == "10.0.0.5" && f->endpoint.port == 28961 &&
           f->rconPassword == "pw" && f->game == Game::COD2);
    assert(!f->ftp.active && f->ftp.port == 21 && f->ftp.codPath == "/");  // no FTP: defaults

    // FTP settings (from the original "Server ändern" window)
    ServerList c;
    e.ftp.active = true;
    e.ftp.passive = true;
    e.ftp.host = "45.131.148.155";
    e.ftp.port = 2121;
    e.ftp.user = "tomyi";
    e.ftp.password = "jel szo";
    e.ftp.codPath = "/cod4/main";
    e.ftp.pbssPath = "/cod4/pb/svss";
    e.ftp.pbssAuto = true;
    c.entries().push_back(e);
    assert(c.save("test_servers.ini"));
    ServerList d;
    assert(d.load("test_servers.ini"));
    const FtpSettings& x = d.entries().at(0).ftp;
    assert(x.active && x.passive && x.host == "45.131.148.155" && x.port == 2121 && x.user == "tomyi" &&
           x.password == "jel szo" && x.codPath == "/cod4/main" && x.pbssPath == "/cod4/pb/svss" && x.pbssAuto);
    std::remove("test_servers.ini");

    // FTP paths
    assert(ftpNormalize("") == "/" && ftpNormalize("cod4//main/") == "/cod4/main" && ftpNormalize("\\a\\b") == "/a/b");
    assert(ftpJoin("/", "main") == "/main" && ftpJoin("/cod4/", "server.cfg") == "/cod4/server.cfg");
    assert(ftpParent("/cod4/main") == "/cod4" && ftpParent("/cod4") == "/" && ftpParent("/") == "/");
}

int main() {
    testStatus();
    testRconPacket();
    testCommands();
    testGame();
    testConfig();
    std::puts("minden teszt rendben");
    return 0;
}
