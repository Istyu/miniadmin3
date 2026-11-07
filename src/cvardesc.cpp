#include "miniadmin/cvardesc.hpp"

#include <cctype>
#include <map>

namespace miniadmin {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool starts(const std::string& s, const std::string& p, std::string& rest) {
    if (s.size() <= p.size() || s.compare(0, p.size(), p) != 0) return false;
    rest = s.substr(p.size());
    return true;
}

// Weapons and grenades as they appear in the game (CoD1/UO/2: scr_allow_*, CoD4/5: weap_allow_*).
const std::map<std::string, std::string>& weaponNames() {
    static const std::map<std::string, std::string> m = {
        // CoD4
        {"m16", "M16A4"}, {"ak47", "AK-47"}, {"m4", "M4 Carbine"}, {"g3", "G3"}, {"g36c", "G36C"}, {"m14", "M14"},
        {"mp44", "MP44"}, {"mp5", "MP5"}, {"skorpion", "Skorpion"}, {"uzi", "Mini-Uzi"}, {"ak74u", "AK-74u"},
        {"p90", "P90"}, {"m1014", "M1014"}, {"winchester1200", "W1200"}, {"saw", "M249 SAW"}, {"rpd", "RPD"},
        {"m60e4", "M60E4"}, {"dragunov", "Dragunov"}, {"m40a3", "M40A3"}, {"barrett", "Barrett .50cal"},
        {"remington700", "R700"}, {"m21", "M21"}, {"beretta", "M9"}, {"colt45", "M1911 .45"}, {"usp", "USP .45"},
        {"deserteagle", "Desert Eagle"}, {"deserteaglegold", "Gold Desert Eagle"}, {"frag_grenade", "Frag Grenade"},
        {"concussion_grenade", "Stun Grenade"}, {"flash_grenade", "Flash Grenade"}, {"smoke_grenade", "Smoke Grenade"},
        // CoD5
        {"svt40", "SVT-40"}, {"gewehr43", "Gewehr 43"}, {"m1garand", "M1 Garand"}, {"stg44", "STG-44"},
        {"m1carbine", "M1A1 Carbine"}, {"thompson", "Thompson"}, {"mp40", "MP40"}, {"type100smg", "Type 100"},
        {"ppsh", "PPSh-41"}, {"doublebarreledshotgun", "Double-Barreled Shotgun"}, {"shotgun", "M1897 Trench Gun"},
        {"30cal", "Browning M1919"}, {"bar", "BAR"}, {"dp28", "DP-28"}, {"fg42", "FG42"}, {"mg42", "MG42"},
        {"type99lmg", "Type 99 LMG"}, {"kar98k", "Kar98k"}, {"mosinrifle", "Mosin-Nagant"}, {"ptrs41", "PTRS-41"},
        {"springfield", "Springfield"}, {"type99rifle", "Arisaka"}, {"colt", "Colt M1911"}, {"nambu", "Nambu"},
        {"walther", "Walther P38"}, {"tokarev", "Tokarev TT-33"}, {"357magnum", ".357 Magnum"},
        {"sticky_grenade", "Sticky Grenade"}, {"molotov", "Molotov Cocktail"}, {"tabun_gas", "Tabun Gas"},
        {"signal_flare", "Signal Flare"},
        // CoD1 / UO / CoD2
        {"bren", "Bren"}, {"enfield", "Lee-Enfield"}, {"enfieldsniper", "Lee-Enfield (Scoped)"}, {"g43", "Gewehr 43"},
        {"greasegun", "Grease Gun"}, {"kar98ksniper", "Kar98k (Scoped)"}, {"nagant", "Mosin-Nagant"},
        {"nagantsniper", "Mosin-Nagant (Scoped)"}, {"panzerfaust", "Panzerfaust"}, {"pps42", "PPS-42"},
        {"sten", "Sten"}, {"fraggrenades", "Frag Grenades"}, {"smokegrenades", "Smoke Grenades"},
        {"pistols", "Pistols"}, {"satchel", "Satchel Charge"}, {"smoke", "Smoke Grenades"}, {"grenades", "Grenades"},
        {"flamethrower", "Flamethrower"}, {"artillery", "Artillery"}, {"bazooka", "Bazooka"}, {"mg34", "MG34"},
        {"mg30cal", "M1919 .30cal"}};
    return m;
}

const std::map<std::string, std::string>& attachmentNames() {
    static const std::map<std::string, std::string> m = {
        {"gl", "Grenade Launcher"}, {"reflex", "Red Dot Sight"}, {"silencer", "Silencer"}, {"acog", "ACOG Scope"},
        {"grip", "Grip"}, {"aperture", "Aperture Sight"}, {"telescopic", "Telescopic Sight"},
        {"flash", "Flash Hider"}, {"bigammo", "Bigger Mag"}, {"silenced", "Suppressor"}, {"bayonet", "Bayonet"},
        {"sawoff", "Sawed Off"}, {"bipod", "Bipod"}};
    return m;
}

const std::map<std::string, std::string>& perkNames() {
    static const std::map<std::string, std::string> m = {
        {"parabolic", "Eavesdrop"}, {"gpsjammer", "UAV Jammer"}, {"holdbreath", "Iron Lungs"},
        {"quieter", "Dead Silence"}, {"longersprint", "Extreme Conditioning"}, {"detectexplosive", "Bomb Squad"},
        {"explosivedamage", "Sonic Boom"}, {"pistoldeath", "Last Stand"}, {"grenadepulldeath", "Martyrdom"},
        {"bulletdamage", "Stopping Power"}, {"bulletpenetration", "Deep Impact"}, {"bulletaccuracy", "Steady Aim"},
        {"rof", "Double Tap"}, {"fastreload", "Sleight of Hand"}, {"extraammo", "Bandolier"},
        {"armorvest", "Juggernaut"}, {"fraggrenade", "Frag x 3"}, {"specialgrenade", "Special Grenades x 3"},
        {"c4_mp", "C4 x 2"}, {"claymore_mp", "Claymore x 2"}, {"rpg_mp", "RPG-7 x 2"},
        {"weapon_bouncing_betty", "Bouncing Betty x 2"}, {"weapon_flamethrower", "M2 Flamethrower"},
        {"weapon_bazooka", "Bazooka"}, {"weapon_satchel_charge", "Satchel Charge x 2"},
        {"twoprimaries", "Overkill"}, {"flakjacket", "Flak Jacket"}, {"fireproof", "Fireproof"},
        {"reconnaissance", "Reconnaissance"}, {"gas_mask", "Gas Mask"}, {"pin_back", "Toss Back"},
        {"water_cooled", "Water Cooled"}, {"greased_barrings", "Greased Bearings"}, {"ordinance", "Ordnance Training"},
        {"leadfoot", "Leadfoot"}};
    return m;
}

const std::map<std::string, std::string>& classNames() {
    static const std::map<std::string, std::string> m = {
        {"assault", "Assault"}, {"specops", "Spec Ops"}, {"heavygunner", "Heavy Gunner"},
        {"demolitions", "Demolitions"}, {"sniper", "Sniper"}, {"rifleman", "Rifleman"},
        {"lightgunner", "Light Gunner"}, {"closeassault", "Close Assault"}};
    return m;
}

std::string nameOr(const std::map<std::string, std::string>& m, const std::string& k) {
    auto it = m.find(k);
    return it == m.end() ? k : it->second;
}

// CoD4: the attachments per weapon category (to be translated), CoD5: per class (class name).
std::string attachGroup(const std::string& g) {
    static const char* const cats[] = {"assault", "smg", "shotgun", "lmg", "sniper", "pistol"};
    for (const char* c : cats)
        if (g == c) return std::string("@wpcat.") + c;
    return nameOr(classNames(), g);
}

}  // namespace

bool describeLoadoutCvar(const std::string& nameIn, CvarDesc& out) {
    const std::string n = lower(nameIn);
    std::string rest;
    out = CvarDesc{};
    if (starts(n, "weap_allow_", rest) || starts(n, "scr_allow_", rest)) {
        out = {"wpvar.weapon", {nameOr(weaponNames(), rest)}};
        return true;
    }
    if (starts(n, "attach_allow_", rest)) {
        const size_t u = rest.find('_');
        if (u == std::string::npos) return false;
        const std::string grp = rest.substr(0, u), att = rest.substr(u + 1);
        if (att == "none") out = {"wpvar.attachNone", {attachGroup(grp)}};
        else out = {"wpvar.attach", {attachGroup(grp), nameOr(attachmentNames(), att)}};
        return true;
    }
    if (starts(n, "perk_allow_specialty_", rest) || starts(n, "perk_allow_", rest)) {
        out = {"wpvar.perk", {nameOr(perkNames(), rest)}};
        return true;
    }
    if (n == "perk_flakjacketmaxdamage") { out = {"wpvar.flakjacketmax", {}}; return true; }
    if (n == "player_laststandbleedouttime") { out = {"wpvar.laststand", {}}; return true; }
    if (starts(n, "perk_", rest) && perkNames().count(rest)) {
        out = {"wpvar.perkvalue", {perkNames().at(rest)}};
        return true;
    }
    if (starts(n, "class_", rest)) {
        for (const auto& c : classNames()) {
            std::string item;
            if (!starts(rest, c.first + "_", item)) continue;
            if (item == "allowdrop") out = {"wpvar.allowdrop", {c.second}};
            else if (item == "limit") out = {"wpvar.classlimit", {c.second}};
            else if (item == "movespeed") out = {"wpvar.movespeed", {c.second}};
            else {
                static const char* const items[] = {"primary", "primary_attachment", "secondary",
                                                    "secondary_attachment", "perk", "perk2", "perk3",
                                                    "grenade", "camo", "frags", "special"};
                bool known = false;
                for (const char* i : items) known |= item == i;
                if (!known) return false;
                out = {"wpvar.classitem", {c.second, "@wpitem." + item}};
            }
            return true;
        }
    }
    return false;
}

const std::vector<std::string>& loadoutDescKeys() {
    static const std::vector<std::string> k = {
        "wpvar.weapon", "wpvar.attach", "wpvar.attachNone", "wpvar.allowdrop", "wpvar.movespeed",
        "wpvar.classlimit", "wpvar.classitem", "wpvar.perk", "wpvar.perkvalue", "wpvar.flakjacketmax",
        "wpvar.laststand", "wpcat.assault", "wpcat.smg", "wpcat.shotgun", "wpcat.lmg", "wpcat.sniper",
        "wpcat.pistol", "wpitem.primary", "wpitem.primary_attachment", "wpitem.secondary",
        "wpitem.secondary_attachment", "wpitem.perk", "wpitem.perk2", "wpitem.perk3", "wpitem.grenade",
        "wpitem.camo", "wpitem.frags", "wpitem.special"};
    return k;
}

}  // namespace miniadmin
