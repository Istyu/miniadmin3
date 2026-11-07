@echo off
rem Debug build: miniAdmin3.exe + the tests (test_*.exe) + the command line miniadmin.exe, then runs the tests.
rem Output: build\Debug\
setlocal
cd /d "%~dp0"

if not exist build\CMakeCache.txt (
    echo --- CMake beallitas: build mappa ^(Visual Studio 2026, Win32^)
    cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
    if errorlevel 1 goto :error
)

echo --- Forditas: Debug
cmake --build build --config Debug
if errorlevel 1 goto :error

echo --- Tesztek
ctest --test-dir build -C Debug --output-on-failure
if errorlevel 1 goto :error

echo.
echo Kesz: build\Debug\miniAdmin3.exe, a tesztek rendben lefutottak.
pause
exit /b 0

:error
echo.
echo HIBA: a Debug build vagy a tesztek nem sikerultek ^(lasd fent^).
pause
exit /b 1
