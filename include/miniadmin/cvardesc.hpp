// Descriptions of the Weapons / Perks tab variables, built from templates.
//
// In the original weapons / perks files only the French ones have descriptions. Here they are built from the variable name:
// the template is a language-file key (wpvar.*), the substituted parts are either the English names shown in the game
// (weapons, attachments, perks, classes – they appear like this in the game too), or – starting with "@" –
// further language keys (weapon categories wpcat.*, class loadout items wpitem.*).
#pragma once
#include <string>
#include <vector>

namespace miniadmin {

struct CvarDesc {
    std::string key;                // e.g. "wpvar.weapon"
    std::vector<std::string> args;  // %1, %2 ... ("@key" = to be translated)
};

// false if there is no template for the name (then the file's description stays, if any).
bool describeLoadoutCvar(const std::string& name, CvarDesc& out);

// All template and name keys (for checking the language files).
const std::vector<std::string>& loadoutDescKeys();

}  // namespace miniadmin
