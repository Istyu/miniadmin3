// Rights (the Rights part of the original "Server ändern" window): per server a password-protected
// "security system" can be switched on that blocks the non-allowed functions for this server inside the program.
// Purpose: handing the server list to a helper admin with limited rights.
// Important: this is only a restriction inside the program – the rcon password can still be read in servers.ini.
#pragma once
#include <string>

namespace miniadmin {

// The original's 20 rights, in the original's order.
enum class Right {
    Kick, Ban, MapRestart, MapSwitch, RotationEdit, GametypeChange, GametypeConfig, ExecCfg, SetPassword, WeaponEdit,
    VarListEdit, PunkBuster, Pam, Mods, Console, PerkEdit, PbssView, PbssDelete, FtpEdit, FtpView, Count
};
constexpr int kRightCount = (int)Right::Count;

const char* rightKey(Right r);      // the storage key ("kick", "ban", "restart" …)
const char* rightLabel(Right r);    // the original (English) label ("Kick", "Map Restart/Reload" …)

struct Rights {
    bool enabled = false;           // "Security system" switched on
    std::string salt, hash;         // salted SHA-256 hash of the password (the password itself is not stored)
    bool allow[kRightCount];
    Rights() { for (bool& a : allow) a = true; }
};

// with the security system off everything is allowed
inline bool allowed(const Rights& r, Right x) { return !r.enabled || r.allow[(int)x]; }

// The keys of the denied rights separated by commas ("kick,console"), and back. Unknown keys are skipped.
std::string deniedList(const Rights& r);
void setDeniedList(Rights& r, const std::string& list);

// Password: new salt, hash; check. An empty password cannot be set (false).
bool setRightsPassword(Rights& r, const std::string& password);
bool checkRightsPassword(const Rights& r, const std::string& password);

// SHA-256 (hex, lower case) – for the tests too
std::string sha256Hex(const std::string& data);

}  // namespace miniadmin
