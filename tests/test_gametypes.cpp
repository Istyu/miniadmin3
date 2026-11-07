// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include "miniadmin/gametypes.hpp"
using namespace miniadmin;

int main() {
    // file name: <GAME>_<gametype>_<lang>.cfg (the original's format)
    GtFile f;
    assert(parseGametypeFileName("C:\\x\\gametypes\\COD4_war_en.cfg", f) && f.game == "COD4" && f.gametype == "war" &&
           f.lang == "en" && f.path == "C:\\x\\gametypes\\COD4_war_en.cfg");
    assert(parseGametypeFileName("coduo_bas_SE.cfg.bak", f) && f.game == "CODUO" && f.gametype == "bas" && f.lang == "se");
    assert(!parseGametypeFileName("dm.cfg", f) && !parseGametypeFileName("COD4_dm.cfg", f) &&
           !parseGametypeFileName("COD4_a_b_c.cfg", f) && !parseGametypeFileName("COD4__en.cfg", f));

    // content: "name;description" or just "name" (both occur in the original files)
    auto v = parseGametypeList(
        "scr_war_scorelimit;Score limite de la map  \r\n"
        "scr_sd_planttime ;\r\n"
        "scr_sab_hotpotato\r\n"
        "\r\n"
        "koth_spawnDelay;Temps (s)\r\n"
        "nem valtozo;x\r\n"
        "SCR_WAR_SCORELIMIT;duplikátum\r\n"
        "scr_war_timelimit;");
    assert(v.size() == 5);
    assert(v[0].name == "scr_war_scorelimit" && v[0].desc == "Score limite de la map");
    assert(v[1].name == "scr_sd_planttime" && v[1].desc.empty());
    assert(v[2].name == "scr_sab_hotpotato" && v[2].desc.empty());
    assert(v[3].name == "koth_spawnDelay" && v[3].desc == "Temps (s)");
    assert(v[4].name == "scr_war_timelimit");
    assert(parseGametypeList("").empty());
    // codcfg\COD4_fr.cfg: "set name; value" lines, and names like "_Admin"
    auto s = parseGametypeList("set sv_master1; cod4master.activision.com\r\n_Admin;Nom de l'admin\r\nSETA sv_gamespy; 1\r\nset ;x\r\n");
    assert(s.size() == 3 && s[0].name == "sv_master1" && s[0].desc == "cod4master.activision.com");
    assert(s[1].name == "_Admin" && s[2].name == "sv_gamespy" && s[2].desc == "1");

    // buttons and file choice
    std::vector<GtFile> files;
    for (const char* n : {"COD4_war_en.cfg", "COD4_war_fr.cfg", "COD4_dm_de.cfg", "COD4_dm_en.cfg", "COD5_twar_en.cfg",
                          "CODUO_bas_se.cfg"}) {
        GtFile x;
        assert(parseGametypeFileName(n, x));
        files.push_back(x);
    }
    auto gts = gametypesOf(files, "cod4");
    assert(gts.size() == 2 && gts[0] == "dm" && gts[1] == "war");
    assert(gametypesOf(files, "COD2").empty());
    assert(pickGametypeFile(files, "COD4", "war", "fr")->lang == "fr");
    assert(pickGametypeFile(files, "COD4", "war", "hu")->lang == "en");  // no Hungarian: English
    assert(pickGametypeFile(files, "CODUO", "BAS", "es")->lang == "se");  // no English either: whatever there is
    assert(pickGametypeFile(files, "COD4", "koth", "en") == nullptr);

    // Weapons / Perks file names: COD4_en.cfg and COD4en.cfg (no underscore)
    assert(parseLangFileName("x\\weapons\\COD4_en.cfg", f) && f.game == "COD4" && f.lang == "en" && f.gametype.empty());
    assert(parseLangFileName("CODUO_fr.cfg", f) && f.game == "CODUO" && f.lang == "fr");
    assert(parseLangFileName("perks\\COD5nl.cfg", f) && f.game == "COD5" && f.lang == "nl");
    assert(parseLangFileName("coduoDE.cfg", f) && f.game == "CODUO" && f.lang == "de");
    assert(!parseLangFileName("COD4.cfg", f));
    assert(!parseLangFileName("valami.cfg", f) && !parseLangFileName("COD4_a_b.cfg", f));
    {
        std::vector<GtFile> wf;
        for (const char* n : {"COD4_de.cfg", "COD4_en.cfg"}) { GtFile x; assert(parseLangFileName(n, x)); wf.push_back(x); }
        assert(pickGametypeFile(wf, "COD4", "", "hu")->lang == "en" && pickGametypeFile(wf, "cod4", "", "de")->lang == "de");
        assert(pickGametypeFile(wf, "COD5", "", "en") == nullptr);
    }

    // MODs: codcfg\<MOD><lang>.mdv / .mdx
    assert(parseModFileName("x\\codcfg\\AWE-SAMPLEen.mdx", ".mdx", f) && f.game == "AWE-SAMPLE" && f.gametype.empty() &&
           f.lang == "en");
    assert(parseModFileName("myModDE.MDV", ".mdv", f) && f.game == "MYMOD" && f.lang == "de" && modDisplayName("x\\myModDE.MDV", ".mdv") == "myMod");
    assert(!parseModFileName("AWE-SAMPLEen.mdv", ".mdx", f) && !parseModFileName("en.mdx", ".mdx", f) &&
           !parseModFileName("mod12.mdx", ".mdx", f));
    {
        std::vector<GtFile> mf;
        for (const char* n : {"AWE-SAMPLEde.mdx", "AWE-SAMPLEen.mdx"}) { GtFile x; assert(parseModFileName(n, ".mdx", x)); mf.push_back(x); }
        assert(pickGametypeFile(mf, "awe-sample", "", "hu")->lang == "en");  // no Hungarian: English
    }

    // PAM-MOD: codcfg\<GAME>.pmm
    std::vector<std::string> pm;
    assert(parsePamModes("pub\r\nbht\r\n\r\nbht1v1\r\nedom_vet\r\npub\r\n", pm) && pm.size() == 4 && pm[0] == "pub" &&
           pm[3] == "edom_vet");
    assert(!parsePamModes("no\r\nPAM mod\r\nfor\r\nthis time\r\nsupported\r\n", pm) && pm.empty());  // COD5.pmm
    assert(!parsePamModes("", pm));

    // description keys
    assert(gametypeVarKey("scr_war_scorelimit", "war") == "gtvar.scorelimit");
    assert(gametypeVarKey("scr_bas_respawn_wave_time", "BAS") == "gtvar.respawn_wave_time");
    assert(gametypeVarKey("koth_spawnDelay", "koth") == "gtvar.koth_spawndelay");

    // built-in table
    auto sd = builtinGametypeVars("SD");
    assert(!sd.empty() && sd[0].name == "scr_sd_scorelimit");
    bool hasBomb = false;
    for (auto& x : sd) hasBomb |= x.name == "scr_sd_bombtimer";
    assert(hasBomb);

    // the server's reply
    std::string val;
    assert(parseCvarReply("\"scr_dm_scorelimit\" is:\"100^7\" default:\"50^7\"\n", "scr_dm_scorelimit", val) && val == "100");
    assert(parseCvarReply("\"SCR_DM_TIMELIMIT\" is:\"10.5^7\"", "scr_dm_timelimit", val) && val == "10.5");
    assert(parseCvarReply("\"x\" is \"7\"", "x", val) && val == "7");
    assert(!parseCvarReply("\"valami_mas\" is:\"1^7\"", "scr_dm_scorelimit", val));
    assert(!parseCvarReply("Bad rconpassword.", "scr_dm_scorelimit", val));
    std::puts("gametypes ok");
}
