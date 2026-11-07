# miniAdmin3

A modern, ground-up recreation of **miniAdmin3**, the graphical remote administration tool for **Call of Duty** dedicated servers (CoD1, CoD:UO, CoD2, CoD4, and CoD:WaW). 

This project pairs a modern, native Win32 user interface with a custom C++ core (`miniadmin_core`) reverse-engineered from the original Delphi / Indy 9 binaries (`miniAdmin3 0.937`).

---

## 🌟 Key Features

* **Multi-Game Support:** Native handling for Call of Duty 1, United Offensive, CoD 2, CoD 4, and CoD: WaW server protocols.
* **Full RCON & Server Control:** Execute commands, manage maps, rotate game modes, and tweak server CVARs in real-time.
* **Enhanced Player Grid & GUID DB:** Instant status parsing, player kick/ban integration, and alias tracking backed by `guids.dat` compatibility.
* **Built-in Master Server Browser:** Query master servers for active games with multi-threaded `getinfo` scanning, live player count tracking, and full filtering by region, ping, map, or gametype.
* **Integrated FTP Client:** Edit remote server configurations directly via built-in multi-threaded FTP with automated passive failover.
* **Global Hotkeys & Quick-Commando:** System-wide keyboard shortcuts for quick game-restarts, map changes, or emergency process termination while playing.
* **Multilingual UI (6 Languages):** Full support for English, Hungarian, German, French, Dutch, and Spanish.
* **IP Geolocation & Provider Lookup:** Automatic country flag and reverse DNS lookups powered by `geoip.csv` (DB-IP) or legacy `ipranges.dat`.
* **Security & Rights System:** Per-server password-protected permission model to delegate control to co-administrators safely (utilizing salted SHA-256 hashing).

---

## 🛠️ Data Files & Setup

Place the following assets in the same directory as `miniAdmin3.exe` for full functionality:

| File / Folder | Purpose |
| :--- | :--- |
| `lang/` | UI localization files (`.lng`) |
| `geoip.csv` | Modern country geolocation data (DB-IP / IP2Location) |
| `ipranges.dat` | Legacy fallback IP ranges |
| `guids.dat` | GUID & Player Alias database |
| `flags/` | Country flag bitmaps (`.bmp`) |
| `mappic/` | Map preview images (`<GAME><map>.jpg`) |
| `gametypes/` | Game mode definition lists |
| `weapons/` & `perks/` | Weapon and perk CVAR control templates |
| `codcfg/` | Game configuration, PAM-MOD (`.pmm`), and MOD templates (`.mdx`, `.mdv`) |

---

## 💻 Building from Source

### Requirements
* **OS:** Windows
* **Compiler:** Visual Studio 2022 / 2026 or MinGW-w64 (with `posix` thread model)
* **Build Tool:** CMake 3.14+

### Quick Build (Release)
Run the provided batch script to build the optimized binary:
```cmd
make_release.cmd
```
The compiled output will be placed in `build\Release\miniAdmin3.exe`.

### Debug Build & Tests
To build all developer tools and test executables:
```cmd
make_debug.cmd
```

### Manual CMake Configuration
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --target miniadmin_gui
```

---

## 📜 Credits & Acknowledgments

* **Original Program:** *miniAdmin3 0.937* by Supermuckl (Deois Gaming).
* **Reverse Engineering & Core Logic:** Reconstructed from original binary disassembly (Delphi / Indy 9 RTL).
* **IP Geolocation:** This product includes IP Geolocation data created by DB-IP, available from [https://db-ip.com](https://db-ip.com).

---

## 📄 License

Distributed under project-specific open/recreation guidelines. See source files for individual module headers.