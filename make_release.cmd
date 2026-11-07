@echo off
rem Release build: only the program (miniAdmin3.exe) and the lang folder, without tests and the helper tool.
rem Output: build\Release\  (the data files there - servers.ini, guids.dat etc. - are not touched)
setlocal
cd /d "%~dp0"

if not exist build\CMakeCache.txt (
    echo --- CMake beallitas: build mappa ^(Visual Studio 2026, Win32^)
    cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
    if errorlevel 1 goto :error
)

echo --- Forditas: Release
cmake --build build --config Release --target miniadmin_gui
if errorlevel 1 goto :error

rem Only the program (miniadmin_gui -> miniAdmin3.exe) and what it needs are built: not the tests and miniadmin.exe.
rem Delete developer programs left over from earlier Release builds (they are now built in Debug only).
for %%f in (build\Release\test_*.exe build\Release\test_*.pdb build\Release\miniadmin.exe build\Release\miniadmin.pdb) do (
    if exist "%%f" del /q "%%f"
)

echo.
echo Kesz: build\Release\miniAdmin3.exe
pause
exit /b 0

:error
echo.
echo HIBA: a Release build nem sikerult ^(lasd fent^). Ha a program fut, zard be es probald ujra.
pause
exit /b 1
