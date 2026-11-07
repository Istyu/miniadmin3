#include "miniadmin/commands.hpp"

namespace miniadmin::cmd {

std::string sanitize(std::string s) {
    for (char& c : s)
        if (c == '"') c = '\'';
    return s;
}

std::string say(const std::string& m) { return "say \"" + sanitize(m) + "\""; }

std::string sayAs(const std::string& admin, const std::string& m) {
    return "say \"" + sanitize(admin) + ": ^7" + sanitize(m) + "\"";
}

std::string tell(int slot, const std::string& m) {
    return "tell " + std::to_string(slot) + " \"" + sanitize(m) + "\"";
}

std::string clientkick(int slot) { return "clientkick " + std::to_string(slot); }
std::string banclient(int slot) { return "banclient " + std::to_string(slot); }
std::string tempbanclient(int slot) { return "tempbanclient " + std::to_string(slot); }

std::string pbKick(const std::string& who, const std::string& admin,
                   const std::string& reason) {
    return "PB_SV_kick \"" + sanitize(who) + "\" 0 \"^7kicked by " + sanitize(admin) +
           " ^7reason: " + sanitize(reason) + "\"";
}

std::string pbBan(const std::string& who, const std::string& admin,
                  const std::string& reason) {
    // original fragments: PB_SV_Ban "  +  " "^7banned by   +  ^7reason:
    return "PB_SV_Ban \"" + sanitize(who) + "\" \"^7banned by " + sanitize(admin) +
           " ^7reason: " + sanitize(reason) + "\"";
}

std::string pbBanGuid(const std::string& guid, const std::string& name,
                      const std::string& ip, const std::string& reason) {
    return "PB_SV_BanGuid \"" + sanitize(guid) + "\" \"" + sanitize(name) + "\" \"" +
           sanitize(ip) + "\" \"" + sanitize(reason) + "\"";
}

std::string pbUnbanGuid(const std::string& guid) {
    return "PB_SV_unbanguid \"" + sanitize(guid) + "\"";
}

std::string pbPlist() { return "pb_sv_plist"; }
std::string pbUpdate() { return "pb_sv_update"; }
std::string pbGetss(const std::string& slot) { return "pb_sv_getss " + slot; }
std::string pbRestart() { return "pb_sv_restart"; }

std::string setGPassword(const std::string& pw) {
    return "set g_password \"" + sanitize(pw) + "\"";
}

std::string setGametype(const std::string& gt) {
    // only letters, digits and underscore may stay: the game mode name must not break out of the command
    std::string clean;
    for (char ch : gt)
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_') clean += ch;
    return "set g_gametype " + clean;
}

std::string map(const std::string& name) { return "map " + name; }
std::string mapRestart() { return "map_restart"; }
std::string fastRestart() { return "fast_restart"; }

std::string querySvMaprotation() { return "sv_maprotation"; }
std::string dirMaps(bool d3dbsp) {
    return d3dbsp ? "dir maps/mp d3dbsp" : "dir maps/mp .bsp";
}

}  // namespace miniadmin::cmd
