@echo off
mkdir build
cd build
cmake -G"Visual Studio 18 2026" -A Win32 ..
::cmake .. -G "Visual Studio 17 2022" -A x64
pause
::timeout /t 10