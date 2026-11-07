// Alias database: GUID -> the names the player used before.
//
// NOTE: this is our OWN simple text format (aliases.db, one "guid<TAB>name" per line) that keeps every
// name of a GUID. The original guids.dat (0x520744 / 0x5208F0, a typed binary file) keeps only the first
// name; it is read and written byte-compatibly by guiddb.hpp.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace miniadmin {

class AliasDb {
public:
    bool load(const std::string& path);   // false if it cannot be read
    bool save(const std::string& path) const;

    // true if a new (guid, name) pair was added. An invalid GUID ("", "0") is not stored.
    bool add(const std::string& guid, const std::string& name);
    // The earlier names of the GUID in arrival order, except exceptName.
    std::vector<std::string> aliases(const std::string& guid, const std::string& exceptName = "") const;

    // Reads the original guids.dat (read only, the file is not modified): 98-byte records,
    //   ShortString[32] guid, ShortString[40] name, ShortString[15] IP, double (TDateTime) last seen.
    // Returns the number of newly added (guid, name) pairs; -1 if unreadable / wrong size.
    int importGuidsDat(const std::string& path);

    static bool validGuid(const std::string& guid);

private:
    std::map<std::string, std::vector<std::string>> db_;
};

}  // namespace miniadmin
