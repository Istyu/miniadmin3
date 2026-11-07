#include "miniadmin/game.hpp"
#include <algorithm>
#include <cctype>

namespace miniadmin {

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

const char* gameTag(Game g) {
    switch (g) {
        case Game::COD1:  return "COD1";
        case Game::CODUO: return "CODUO";
        case Game::COD2:  return "COD2";
        case Game::COD4:  return "COD4";
        case Game::COD5:  return "COD5";
        default:          return "unknown";
    }
}

const char* gameName(Game g) {
    switch (g) {
        case Game::COD1:  return "Call of Duty";
        case Game::CODUO: return "CoD:United Offensive";
        case Game::COD2:  return "Call of Duty 2";
        case Game::COD4:  return "Call of Duty 4";
        case Game::COD5:  return "Call of Duty: World at War";
        default:          return "unknown";
    }
}

Game gameFromTag(const std::string& tag) {
    const std::string t = lower(tag);
    if (t == "cod1")  return Game::COD1;
    if (t == "coduo") return Game::CODUO;
    if (t == "cod2")  return Game::COD2;
    if (t == "cod4")  return Game::COD4;
    if (t == "cod5")  return Game::COD5;
    return Game::Unknown;
}

Game gameFromGamename(const std::string& gamename) {
    const std::string n = lower(gamename);
    // Order matters: the more specific names first.
    if (n.find("world at war") != std::string::npos)     return Game::COD5;
    if (n.find("united offensive") != std::string::npos) return Game::CODUO;
    if (n.find("call of duty 4") != std::string::npos)   return Game::COD4;
    if (n.find("call of duty 2") != std::string::npos)   return Game::COD2;
    if (n.find("call of duty") != std::string::npos)     return Game::COD1;
    return gameFromTag(gamename);
}

}  // namespace miniadmin
