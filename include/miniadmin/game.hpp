// Supported Call of Duty versions (based on the original function at 0x517B34).
#pragma once
#include <string>

namespace miniadmin {

enum class Game { Unknown, COD1, CODUO, COD2, COD4, COD5 };

// The original program's internal tags: "COD1", "CODUO", "COD2", "COD4", "COD5"
const char* gameTag(Game g);
// Display name: "Call of Duty", "CoD:United Offensive", ...
const char* gameName(Game g);
Game gameFromTag(const std::string& tag);
// Infers the game from the \gamename\ field.
Game gameFromGamename(const std::string& gamename);

}  // namespace miniadmin
