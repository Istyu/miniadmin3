// RCON command builders. The texts come from strings found in the original
// binary (the source address is in the comments).
#pragma once
#include <string>

namespace miniadmin::cmd {

// 0x529C90:  say "<message>"
std::string say(const std::string& message);
// 0x529C90 (admin message):  say "<admin>: ^7<message>"
std::string sayAs(const std::string& admin, const std::string& message);
// 0x5299BC:  tell <slot> "<message>"
std::string tell(int slot, const std::string& message);

// 0x5290BC / 0x5293C8 / 0x5296D8 (HIGHPINGER, LOWSTATS, HIGHSTATS auto-kick)
std::string clientkick(int slot);                 // "clientkick N"
std::string banclient(int slot);                  // "banclient N"
std::string tempbanclient(int slot);              // "tempbanclient N"

// PunkBuster:  PB_SV_kick "<slot>" 0 "^7kicked by <admin> ^7reason: <reason>"
std::string pbKick(const std::string& slotOrName, const std::string& admin,
                   const std::string& reason);
// PB_SV_Ban "<slot>" "^7banned by <admin> ^7reason: <reason>"   (0x5283FC)
std::string pbBan(const std::string& slotOrName, const std::string& admin,
                  const std::string& reason);
// PB_SV_BanGuid "<guid>" "<name>" "<ip>" "<reason>"   (0x510273)
std::string pbBanGuid(const std::string& guid, const std::string& name,
                      const std::string& ip, const std::string& reason);
// PB_SV_unbanguid "<guid>"                        (0x511B71)
std::string pbUnbanGuid(const std::string& guid);
std::string pbPlist();      // pb_sv_plist    (0x52D4EC)
std::string pbUpdate();     // pb_sv_update   (0x52CE60)
std::string pbGetss(const std::string& slot);  // pb_sv_getss (0x52CF04)
std::string pbRestart();    // pb_sv_restart  (0x52CDF0)

// 0x50C6B4:  set g_password "<password>"
std::string setGPassword(const std::string& pw);
// Sets the game mode (takes effect from the next map change): "set g_gametype <dm|dom|koth|sab|sd|war ...>"
std::string setGametype(const std::string& gametype);
// map / map_restart / fast_restart  (Button2/3/4/9Click)
std::string map(const std::string& name);
std::string mapRestart();
std::string fastRestart();
// 0x513340 (map list): "sv_maprotation" query + "dir maps/mp .bsp"
std::string querySvMaprotation();
std::string dirMaps(bool d3dbsp);  // COD2/COD4/COD5: d3dbsp, COD1/UO: bsp (as in the original)

// Quote protection. NOTE: this is NOT a recreation of the original behaviour
// (that was not examined), but a safety addition: it replaces the " character
// with ' so that user text cannot break out of the rcon quotes.
std::string sanitize(std::string s);

}  // namespace miniadmin::cmd
