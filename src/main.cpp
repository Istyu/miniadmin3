// miniadmin - command line front end for the recreated core library.
// (The original program was a Delphi VCL GUI; the GUI forms (DFM) are not in
//  the IDA .asm, so only the working logic is here.)
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include "miniadmin/commands.hpp"
#include "miniadmin/config.hpp"
#include "miniadmin/hostinfo.hpp"
#include "miniadmin/net.hpp"
#include "miniadmin/status.hpp"

using namespace miniadmin;

static void usage() {
    std::cout <<
        "miniadmin 0.937 (újraalkotott mag)\n\n"
        "Használat:\n"
        "  miniadmin status <host> <port>\n"
        "  miniadmin rcon   <host> <port> <jelszó> <parancs...>\n"
        "  miniadmin say    <host> <port> <jelszó> <üzenet...>\n"
        "  miniadmin kick   <host> <port> <jelszó> <slot>\n"
        "  miniadmin map    <host> <port> <jelszó> <térkép>\n"
        "  miniadmin list   <servers.ini>\n"
        "  miniadmin geo    <ip|host>... [--dir <mappa>]   (ország-adatbázis ellenőrzése; a --dir legyen az utolsó)\n";
}

static std::string joinFrom(int argc, char** argv, int from) {
    std::string s;
    for (int i = from; i < argc; ++i) {
        if (i > from) s += ' ';
        s += argv[i];
    }
    return s;
}

static int cmdStatus(const Endpoint& ep) {
    std::string raw = queryStatus(ep);
    ServerInfo info;
    if (raw.empty() || !parseStatusResponse(raw, info)) {
        std::cerr << "Nincs (érvényes) válasz a szervertől.\n";
        return 2;
    }
    std::cout << "Játék    : " << gameName(info.game) << " (" << gameTag(info.game) << ")\n"
              << "Név      : " << stripColors(info.hostname()) << '\n'
              << "Térkép   : " << info.mapname() << '\n'
              << "Játékmód : " << info.gametype() << '\n'
              << "Játékosok: " << info.players.size() << '/' << info.maxClients() << '\n'
              << "Jelszó   : " << (info.passworded() ? "igen" : "nem") << '\n'
              << "PunkBuster: " << (info.punkbuster() ? "igen" : "nem") << "\n\n";
    for (const auto& p : info.players)
        std::cout << "  #" << p.slot << "  score=" << p.score << "  ping=" << p.ping
                  << "  " << stripColors(p.name) << '\n';
    return 0;
}

static int cmdRcon(const Endpoint& ep, const std::string& pw, const std::string& command) {
    RconClient rc(ep, pw);
    std::string out = rc.execute(command, false);  // sent once: the command must not be multiplied
    if (out.empty()) {
        std::cerr << "Nincs válasz (a parancs egyszer el lett küldve; say/kick/map esetén ez normális, egyébként rossz jelszó vagy elérhetetlen szerver).\n";
        return 2;
    }
    std::cout << out;
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 3) { usage(); return 1; }
    const std::string verb = argv[1];

    if (verb == "list") {
        ServerList sl;
        if (!sl.load(argv[2])) { std::cerr << "Nem olvasható: " << argv[2] << '\n'; return 1; }
        for (const auto& e : sl.entries())
            std::cout << e.name << "  " << e.endpoint.host << ':' << e.endpoint.port
                      << "  " << gameTag(e.game) << '\n';
        return 0;
    }

    if (verb == "geo") {
        std::string dir = ".";
        std::vector<std::string> targets;
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--dir" && i + 1 < argc) {
                dir = joinFrom(argc, argv, i + 1);  // --dir comes last: a folder name with spaces can be given without quotes
                break;
            }
            targets.push_back(argv[i]);
        }
        GeoDb geo;
        const bool dat = geo.loadIpRangesDat(dir + "/ipranges.dat");
        const bool csv = geo.loadCsv(dir + "/geoip.csv");
        std::cout << "mappa       : " << dir << '\n';
        if (!dat && !csv) std::cout << "(egyik fájl sem található ott. Ha a mappa neve szóközt tartalmaz, tedd idézőjelbe,\n"
                                       " vagy a --dir legyen az utolsó paraméter.)\n";
        std::cout << "ipranges.dat: " << (dat ? std::to_string(geo.legacySize()) + " tartomány" : "nincs / hibás") << '\n'
                  << "geoip.csv   : " << (csv ? std::to_string(geo.csvSize()) + " IPv4 tartomány"
                                              : "nincs / ismeretlen formátum (várt: kezdő IP, záró IP, országkód)")
                  << "\n\n";
        for (const auto& t : targets) {
            HostInfo h = lookupHost(t, &geo);
            std::string code, name;
            bool legacy = false;
            const bool hit = !h.ip.empty() && geo.lookup(h.ip, code, name, &legacy);
            std::cout << t << " -> " << (h.ip.empty() ? "(nem feloldható)" : h.ip) << '\n'
                      << "   találat : " << (hit ? (legacy ? "csak a régi ipranges.dat-ban" : "geoip.csv") : "nincs egyik fájlban sem");
            if (hit) std::cout << "  [" << code << " " << name << "]";
            std::cout << "\n   megjelenik: " << (h.country.empty() ? "(nincs ország)" : h.countryCode + " " + h.country)
                      << (h.countryFromHostname ? "  (a reverse DNS végződés alapján javítva)" : "")
                      << "\n   rDNS    : " << (h.hostname.empty() ? "(nincs)" : h.hostname) << "\n\n";
        }
        return 0;
    }

    if (argc < 4) { usage(); return 1; }
    Endpoint ep{argv[2], (uint16_t)std::atoi(argv[3])};

    if (verb == "status") return cmdStatus(ep);
    if (argc < 6) { usage(); return 1; }
    const std::string pw = argv[4];

    if (verb == "rcon") return cmdRcon(ep, pw, joinFrom(argc, argv, 5));
    if (verb == "say")  return cmdRcon(ep, pw, cmd::say(joinFrom(argc, argv, 5)));
    if (verb == "kick") return cmdRcon(ep, pw, cmd::clientkick(std::atoi(argv[5])));
    if (verb == "map")  return cmdRcon(ep, pw, cmd::map(argv[5]));

    usage();
    return 1;
}
