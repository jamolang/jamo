@echo off
rem Builds jamo.exe beside this file. Needs g++ on the PATH, or WinLibs installed with winget.
rem
rem jamo.exe is the compiler written in Jamo (compiler\jamo.jamo). It's built in three steps:
rem   1. g++ builds the C++ compiler, kept as jamo_cpp.exe, so Jamo can always be rebuilt from scratch
rem   2. jamo_cpp.exe builds the compiler written in Jamo, which becomes jamo.exe
rem   3. jamo.exe builds itself again, and the two must be the same file, byte for byte
setlocal
cd /d "%~dp0"
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
g++ -std=c++20 -O2 -Wall -Wextra -Wconversion -static -municode src\main.cpp src\modules.cpp src\libraries.cpp src\pe.cpp src\codegen.cpp src\runtime.cpp src\checker.cpp src\parser.cpp src\lexer.cpp src\errors.cpp -o jamo_cpp.exe || exit /b 1
echo Built jamo_cpp.exe, the C++ compiler
"%~dp0jamo_cpp.exe" build compiler\jamo.jamo || exit /b 1
move /y compiler\jamo.exe jamo.exe >nul || exit /b 1
"%~dp0jamo.exe" build compiler\jamo.jamo || exit /b 1
fc /b jamo.exe compiler\jamo.exe >nul
if errorlevel 1 (
    echo The compiler written in Jamo built itself differently from the C++ build, so something is wrong. 1>&2
    del compiler\jamo.exe
    exit /b 1
)
del compiler\jamo.exe
echo Built jamo.exe, the compiler written in Jamo, and it builds itself the same
