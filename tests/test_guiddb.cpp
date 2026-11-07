// Build: g++ -std=c++17 -Iinclude src/guiddb.cpp src/status.cpp src/game.cpp tests/test_guiddb.cpp
// With a real guids.dat (argv[1]) it also checks the byte-exact round trip.
// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "miniadmin/guiddb.hpp"
using namespace miniadmin;

static std::string slurp(const char* p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    // date formatting (from the screenshot: 2019. 09. 15. 15:15:49)
    assert(formatTDateTime(43723.63598811343) == "2019. 09. 15. 15:15:49");
    assert(formatTDateTime(46327.96719207176) == "2026. 11. 01. 23:12:45");
    assert(formatTDateTime(0.0).empty());
    assert(ipWithoutPort("1.2.3.4:28960") == "1.2.3.4" && ipWithoutPort("1.2.3.4") == "1.2.3.4");
    const double n = nowTDateTime();
    assert(n > 46000 && n < 60000);  // between 2025 and 2064

    GuidDb db;
    assert(!db.touch("0", "x", "1.1.1.1", 46000) && !db.touch("", "x", "1.1.1.1", 46000));
    assert(db.touch("123456", "^1Teszt", "1.2.3.4", 46000.5) && db.size() == 1 && db.dirty());
    db.markClean();
    assert(!db.touch("123456", "^1Teszt", "1.2.3.4", 46000.5 + 10.0 / 86400));  // 10 s: not a modification
    assert(!db.dirty());
    assert(db.touch("123456", "^1Teszt", "1.2.3.4", 46000.5 + 120.0 / 86400));  // 2 minutes: yes
    // as in the original: the name stays (the one first seen), only the IP and the time are updated
    assert(db.touch("123456", "Uj Nev", "4.3.2.1", 46000.6));
    assert(db.records()[0].name() == "^1Teszt" && db.records()[0].ip() == "4.3.2.1" && db.size() == 1);
    assert(db.search("teszt").size() == 1 && db.search("4.3.2").size() == 1 && db.search("Uj Nev").empty());
    // a new GUID only with the original's conditions: at least 6 characters, only letters and digits, non-empty name
    assert(!db.touch("12345", "rovid", "1.1.1.1", 46000) && !db.touch("12345-7", "jel", "1.1.1.1", 46000));
    assert(!db.touch("abcdef", "   ", "1.1.1.1", 46000) && !db.touch("000000", "bot", "1.1.1.1", 46000));
    assert(db.size() == 1);
    assert(db.touch("  abcDEF12  ", "  Nev  ", "1.1.1.1", 46000) && db.records()[1].guid() == "abcDEF12" &&
           db.records()[1].name() == "Nev");  // surrounding spaces cut off
    assert(db.touch("ABCDEF12", "x", "1.1.1.1", 46000) && db.size() == 3);  // case matters: this is a new GUID
    db = GuidDb();
    assert(db.touch("123456", "^1Teszt", "4.3.2.1", 46000.6));
    assert(db.touch("555abc", "\xC1rv\xEDzt\xFBr\xF5", "2.2.2.2", 46000.7));          // cp1252: Árvíztűrő
    assert(db.search("\xE1RV").size() == 1);                                        // á == Á
    assert(db.touch("999abc", "^3ki^5ng", "5.6.7.8", 46001));
    assert(db.search("king").size() == 1);  // found without the colour code too

    GuidDb o;
    o.touch("999abc", "ki ng újabb", "5.6.7.9", 46010);  // newer -> updates
    o.touch("123456", "régebbi", "9.9.9.9", 40000);    // older -> skipped
    o.touch("777abc", "új", "7.7.7.7", 46002);            // new
    auto m = db.merge(o);
    assert(m.added == 1 && m.updated == 1 && m.skipped == 1 && db.size() == 4);

    assert(db.save("t_g.dat"));
    GuidDb back;
    assert(back.load("t_g.dat") && back.size() == 4 && back.records()[0].guid() == "123456");
    std::remove("t_g.dat");

    if (argc > 1) {  // a real original file
        GuidDb real;
        assert(real.load(argv[1]));
        std::printf("betöltve: %zu rekord\n", real.size());
        assert(real.save("t_real.dat"));
        assert(slurp(argv[1]) == slurp("t_real.dat"));  // byte-identical
        std::puts("bájtpontos körbejárás: rendben");
        // after a modification the other records stay untouched
        const std::string before = slurp(argv[1]);
        // an existing GUID from the first record, with another name and a later time: only the time (and the IP) may change
        const GuidRec& r0 = real.records().at(0);
        assert(real.touch(r0.guid(), "MasNev", r0.ip(), nowTDateTime() + 1.0));
        assert(real.save("t_real.dat"));
        const std::string after = slurp("t_real.dat");
        assert(after.size() == before.size());
        size_t diff = 0;
        size_t first = before.size(), last = 0;
        for (size_t i = 0; i < before.size(); ++i)
            if (before[i] != after[i]) { ++diff; first = std::min(first, i); last = i; }
        std::fprintf(stderr, "elterő bájtok: %zu (rekord %zu-%zu, rekordon belül %zu..%zu)\n", diff, first / 98, last / 98,
                     first % 98, last % 98);
        // only one record could change, and not its name (33..73)
        assert(diff > 0 && first / 98 == last / 98 && (first % 98 >= 74 || last % 98 < 33));
        std::remove("t_real.dat");
    }
    std::puts("guiddb ok");
}
