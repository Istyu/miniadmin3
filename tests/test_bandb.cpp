// Build: g++ -std=c++17 -Iinclude src/bandb.cpp src/guiddb.cpp src/status.cpp src/game.cpp tests/test_bandb.cpp
// With a real lastbans.db (argv[1]) it also checks the byte-exact round trip.
// keep assert active in Release too
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "miniadmin/bandb.hpp"
#include "miniadmin/guiddb.hpp"
using namespace miniadmin;

static std::string slurp(const char* p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    // new entries: ASCII and accented (UTF-8) name, add/delete, save + read back
    BanDb db;
    assert(!db.add("rövid", "123", "1.2.3.4:28960", 46000.5));  // a GUID shorter than 10 characters is not recorded
    assert(db.add("Teszt", "0123456789abcdef0123456789abcdef", "1.2.3.4:28960", 46000.123456));
    assert(db.add("Nevetgél", "fedcba9876543210fedcba9876543210", "5.6.7.8:23028", 46327.96719207176));
    assert(db.records().size() == 2 && db.dirty());
    assert(db.save("t_b.db"));
    BanDb back;
    assert(back.load("t_b.db") && back.records().size() == 2);
    assert(back.records()[0].nick == "Teszt" && back.records()[0].server == "1.2.3.4:28960");
    assert(std::fabs(back.records()[0].when - 46000.123456) < 1e-6);
    assert(back.records()[1].nick == "Nevetgél" && back.records()[1].guid == "fedcba9876543210fedcba9876543210");
    assert(formatTDateTime(back.records()[1].when) == "2026. 11. 01. 23:12:45");
    assert(back.removeAt(0) && back.records().size() == 1 && !back.removeAt(5));
    std::remove("t_b.db");

    BanDb none;
    assert(!none.load("nincs_ilyen.db") && none.records().empty());
    { std::ofstream f("t_bad.db", std::ios::binary); f << "ez nem lastbans"; }
    assert(!none.load("t_bad.db"));
    std::remove("t_bad.db");

    if (argc > 1) {
        BanDb real;
        assert(real.load(argv[1]));
        std::printf("betöltve: %zu bejegyzés\n", real.records().size());
        for (const auto& r : real.records())
            std::printf("  %s | %s | %s | %s\n", r.guid.c_str(), r.nick.c_str(), r.server.c_str(), formatTDateTime(r.when).c_str());
        assert(real.save("t_real.db"));
        assert(slurp(argv[1]) == slurp("t_real.db"));  // byte-identical
        // after adding a new entry the old one's bytes are untouched, and the new one can be read back too
        real.add("Új játékos", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "9.9.9.9:28960", 46330.25);
        assert(real.save("t_real.db"));
        BanDb again;
        assert(again.load("t_real.db") && again.records().size() == real.records().size());
        const std::string orig = slurp(argv[1]), now = slurp("t_real.db");
        assert(now.compare(4, orig.size() - 4, orig, 4, std::string::npos) == 0);  // the old entry is byte-identical
        std::remove("t_real.db");
        std::puts("bájtpontos körbejárás: rendben");
    }
    std::puts("bandb ok");
}
