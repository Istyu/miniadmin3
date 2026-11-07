// Server list handling.
//
// NOTE: the original servers.cfg (0x519F28 load / 0x51A464 save) is a
// text file with 7 fields per server, but the exact meaning and order of the fields
// has not been reverse engineered. That is why this module uses its OWN simple INI-like format
// (servers.ini), which is NOT compatible with the original servers.cfg.
//
// Fields of the original server record (TServer), based on 0x513E38 (SendUDPCMD):
//   +0x1D8 byte    active/enabled flag
//   +0x240 string  rcon password
//   +0x248 string  host/IP
//   +0x24C int     port
//   +0x250 double  time of the last command (TDateTime)
//   +0x338 ...     "hide password in log" flag (-> "hidden")
#pragma once
#include <string>
#include <vector>
#include "miniadmin/game.hpp"
#include "miniadmin/net.hpp"
#include "miniadmin/rights.hpp"

namespace miniadmin {

// The FTP part of the original "Server ändern" window (per server).
struct FtpSettings {
    bool active = false;        // FTP active
    bool passive = false;       // Passive FTP mode
    std::string host;           // empty: the server's IP
    int port = 21;
    std::string user, password;
    std::string codPath = "/";  // COD Pfad: the game's folder on the FTP (the editor starts here)
    std::string pbssPath = "/"; // COD PB-Screenshot path
    bool pbssAuto = false;      // PB-SS automatic / recursive (FTP file deletion disabled)
};

struct ServerEntry {
    std::string name;        // displayed name (arbitrary)
    std::string customName;  // if not empty: the given custom name, it is not overwritten by the server's name
    Endpoint endpoint;
    std::string rconPassword;
    Game game = Game::Unknown;
    bool enabled = true;
    bool hidePasswordInLog = true;
    FtpSettings ftp;
    Rights rights;  // Rights (the security system)
};

class ServerList {
public:
    bool load(const std::string& path);   // false if it cannot be read
    bool save(const std::string& path) const;

    std::vector<ServerEntry>& entries() { return entries_; }
    const std::vector<ServerEntry>& entries() const { return entries_; }
    const ServerEntry* find(const std::string& name) const;

private:
    std::vector<ServerEntry> entries_;
};

}  // namespace miniadmin
