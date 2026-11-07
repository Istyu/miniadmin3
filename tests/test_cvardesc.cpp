// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include "miniadmin/cvardesc.hpp"
using namespace miniadmin;

int main() {
    CvarDesc d;
    assert(describeLoadoutCvar("weap_allow_m16", d) && d.key == "wpvar.weapon" && d.args.size() == 1 && d.args[0] == "M16A4");
    assert(describeLoadoutCvar("scr_allow_kar98ksniper", d) && d.args[0] == "Kar98k (Scoped)");
    assert(describeLoadoutCvar("weap_allow_ismeretlen", d) && d.args[0] == "ismeretlen");  // unknown: the code stays
    assert(describeLoadoutCvar("attach_allow_assault_gl", d) && d.key == "wpvar.attach" && d.args[0] == "@wpcat.assault" &&
           d.args[1] == "Grenade Launcher");
    assert(describeLoadoutCvar("attach_allow_rifleman_none", d) && d.key == "wpvar.attachNone" && d.args[0] == "Rifleman");
    assert(describeLoadoutCvar("perk_allow_specialty_bulletdamage", d) && d.key == "wpvar.perk" && d.args[0] == "Stopping Power");
    assert(describeLoadoutCvar("perk_allow_c4_mp", d) && d.args[0] == "C4 x 2");
    assert(describeLoadoutCvar("perk_armorvest", d) && d.key == "wpvar.perkvalue" && d.args[0] == "Juggernaut");
    assert(describeLoadoutCvar("player_lastStandBleedoutTime", d) && d.key == "wpvar.laststand");
    assert(describeLoadoutCvar("class_specops_allowdrop", d) && d.key == "wpvar.allowdrop" && d.args[0] == "Spec Ops");
    assert(describeLoadoutCvar("class_sniper_limit", d) && d.key == "wpvar.classlimit");
    assert(describeLoadoutCvar("class_closeassault_movespeed", d) && d.key == "wpvar.movespeed" && d.args[0] == "Close Assault");
    assert(describeLoadoutCvar("class_heavygunner_secondary_attachment", d) && d.key == "wpvar.classitem" &&
           d.args[0] == "Heavy Gunner" && d.args[1] == "@wpitem.secondary_attachment");
    assert(!describeLoadoutCvar("class_assault_valami", d) && !describeLoadoutCvar("scr_dm_scorelimit", d));
    // every template key is in the list
    for (const char* k : {"wpvar.weapon", "wpvar.attach", "wpcat.assault", "wpitem.secondary_attachment"}) {
        bool found = false;
        for (const auto& x : loadoutDescKeys()) found |= x == k;
        assert(found);
    }
    std::puts("cvardesc ok");
}
