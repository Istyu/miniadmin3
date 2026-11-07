// Gametypes (Gametypes tab): the gametype's variables, their descriptions, and parsing the server's reply.
//
// The original program works from the "gametypes\*.cfg*" files. The file name is <GAME>_<gametype>_<lang>.cfg
// (e.g. COD4_war_en.cfg, the language is the language file's "languageprefix": de, en, es, fr, nl, ...), one
// variable per line: "name;description" (the description is mostly empty). The buttons are the gametypes of the
// selected server's game, the list is the variables of the server's current gametype.
#pragma once
#include <string>
#include <vector>

namespace miniadmin {

struct GtVar {
    std::string name;  // e.g. "scr_dm_scorelimit"
    std::string desc;  // short description (from the file: in the original's code page)
};

struct GtFile {
    std::string game;      // upper-case game: COD1, CODUO, COD2, COD4, COD5
    std::string gametype;  // lower-case gametype: dm, war, ...
    std::string lang;      // lower-case language code: en, de, ...
    std::string path;      // the file (as the caller gave it)
};

// "COD4_war_en.cfg" (with a path, ".cfg.bak" etc. too) -> {COD4, war, en}. false if it is not three parts.
bool parseGametypeFileName(const std::string& fileName, GtFile& out);

// The Weapons / Perks files: weapons\<GAME>_<lang>.cfg and perks\<GAME><lang>.cfg (no underscore),
// e.g. COD4_en.cfg / COD4en.cfg -> {COD4, "", en}. The gametype is empty; same content format.
bool parseLangFileName(const std::string& fileName, GtFile& out);

// File content: one "name;description" or just "name" per line. Empty / non-variable-name lines are skipped,
// duplicates too (case-insensitive).
std::vector<GtVar> parseGametypeList(const std::string& text);

// The game's gametypes (lower case, alphabetical, no repeats) – these are the buttons.
std::vector<std::string> gametypesOf(const std::vector<GtFile>& files, const std::string& game);

// The game + gametype file in the requested language; if there is none: English, then any (the original only
// looked for the exact language, so e.g. in Hungarian – there are no hu files – the list stayed empty). nullptr if none.
const GtFile* pickGametypeFile(const std::vector<GtFile>& files, const std::string& game, const std::string& gametype,
                               const std::string& lang);

// MODs tab: codcfg\<MOD><lang>.mdv (variables) / .mdx (commands), e.g. AWE-SAMPLEen.mdx -> {AWE-SAMPLE, "", en}.
// The language is the two letters before the extension. The game field gets the MOD name (upper case, for lookup);
// the original spelling comes from the file name: modDisplayName. false if the extension / form does not match.
std::string modDisplayName(const std::string& fileName, const std::string& ext);
bool parseModFileName(const std::string& fileName, const std::string& ext, GtFile& out);

// PAM-MOD tab: codcfg\<GAME>.pmm, one pam_mode value per line (pub, bht, ...). false if the file is not
// a mode list – COD5.pmm for example is only a message ("no / PAM mod / for / this time / supported"): mode names
// have no spaces and contain only letters, digits, _ and -.
bool parsePamModes(const std::string& text, std::vector<std::string>& modes);

// Built-in default table (CoD4 variables) when there is no gametypes folder: lower-case name (dm, dom, koth, sab, sd, war).
std::vector<GtVar> builtinGametypeVars(const std::string& gametype);

// The description key in the language file: "gtvar.<suffix>", the suffix is the name without "scr_<gametype>_", lower case
// (scr_war_scorelimit -> gtvar.scorelimit, koth_spawnDelay -> gtvar.koth_spawndelay).
std::string gametypeVarKey(const std::string& varName, const std::string& gametype);

// Reads the value from the reply to a cvar query. The CoD form:
//   "scr_dm_scorelimit" is:"100^7" default:"100^7"
// The trailing "^7" colour code is cut off. false if the reply is not about this variable / has no value.
bool parseCvarReply(const std::string& reply, const std::string& name, std::string& value);

}  // namespace miniadmin
