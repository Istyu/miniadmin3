// GUID database: BYTE-COMPATIBLE with the original miniAdmin3 guids.dat file (reads and writes).
//
// Record (98 bytes, Delphi packed record, little endian):
//   0..32   ShortString[32]  GUID       (length byte + 32 characters)
//   33..73  ShortString[40]  name (when first seen; with colour codes, Windows-1252)
//   74..89  ShortString[15]  last IP
//   90..97  double           last seen (Delphi TDateTime: days since 1899-12-30)
// Unmodified records are written back exactly as they were read (the original program may leave garbage in the
// padding bytes): loading + saving guids.dat gives an unchanged file.
#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace miniadmin {

struct GuidRec {
    static constexpr size_t kSize = 98;
    std::array<unsigned char, kSize> raw{};

    std::string guid() const;
    std::string name() const;
    std::string ip() const;
    double lastSeen() const;
    // Sets the fields (zeroes the padding); long values are truncated.
    void set(const std::string& guid, const std::string& name, const std::string& ip, double lastSeen);
};

class GuidDb {
public:
    bool load(const std::string& path);        // false: unreadable, or not a multiple of 98 / a bad record
    bool save(const std::string& path) const;  // temporary file + replace
    void clear();

    size_t size() const { return recs_.size(); }
    const std::vector<GuidRec>& records() const { return recs_; }

    // A player was seen (the original's rules, sub_520A90): a known GUID only gets its IP and time updated – the
    // name stays the first one; a new record needs a GUID of at least 6 letters/digits and a non-empty name.
    // true if the content changed (dirty). The date is only rewritten after ≥ 60 s, so the per-second refresh does not mark everything modified.
    bool touch(const std::string& guid, const std::string& name, const std::string& ip, double lastSeen);

    struct MergeResult { int added = 0, updated = 0, skipped = 0; };
    // Merges another database: new GUIDs are added; for an existing one the newer "last seen" wins.
    MergeResult merge(const GuidDb& other);

    // Case-insensitive substring search in the GUID, the name (with and without colour codes) and the IP.
    std::vector<size_t> search(const std::string& text, size_t maxResults = 1000) const;

    bool dirty() const { return dirty_; }
    void markClean() { dirty_ = false; }

private:
    void reindex();
    std::vector<GuidRec> recs_;
    std::unordered_map<std::string, size_t> index_;
    bool dirty_ = false;
};

double nowTDateTime();                       // local time, Delphi TDateTime
std::string formatTDateTime(double t);       // "2019. 09. 15. 15:15:49"; invalid: ""
std::string ipWithoutPort(const std::string& addr);  // "1.2.3.4:28960" -> "1.2.3.4"

}  // namespace miniadmin
