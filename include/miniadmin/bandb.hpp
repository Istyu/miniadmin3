// "Recently banned": reads and writes the original miniAdmin3 lastbans.db file COMPATIBLY.
//
// The file is a Delphi TWriter stream:
//   int32  count
//   count × { "TPF0", ShortString "Tban", ShortString "" (name),
//                 properties: ShortString name + value-type byte + value ... , 0 (end),
//                 0 (no child components) }
// Properties: Pn = player name, Pg = GUID, Pi = "ip:port" (the server), Pd = time (Extended TDateTime).
// The raw bytes of the loaded entries are kept and written back unchanged.
#pragma once
#include <string>
#include <vector>

namespace miniadmin {

struct BanRec {
    std::string nick;    // UTF-8, with colour codes
    std::string guid;
    std::string server;  // "ip:port"
    double when = 0;     // Delphi TDateTime
    std::string raw;     // the bytes of an entry loaded from the file (empty: new entry)
};

class BanDb {
public:
    // false: unreadable, or not this format. The list then stays empty.
    bool load(const std::string& path);
    bool save(const std::string& path) const;  // temporary file + replace

    // The original's rule: it only records GUIDs of 10 or more characters (PunkBuster). true if added.
    bool add(const std::string& nickUtf8, const std::string& guid, const std::string& server, double when);
    bool removeAt(size_t i);

    const std::vector<BanRec>& records() const { return recs_; }
    bool dirty() const { return dirty_; }
    void markClean() { dirty_ = false; }

private:
    std::vector<BanRec> recs_;
    bool dirty_ = false;
};

// One entry as a Delphi stream ("TPF0" ... 0, 0). Exposed for testing.
std::string serializeBan(const BanRec& r);

}  // namespace miniadmin
