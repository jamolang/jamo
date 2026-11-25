@echo off
rem Installs Jamo for this Windows user: copies jamo.exe into a folder of its own and adds that folder to your PATH,
rem so "jamo" works in any new terminal. Needs no administrator. Double-click it, or run: install.cmd [folder] [--no-path]
setlocal
rem Double-clicking passes no inputs. Only then does it wait for a key at the end, so the window stays open to be read
set "ASKED=%~1"
set "TARGET=%LOCALAPPDATA%\Programs\Jamo"
set "ADD_PATH=yes"
if not "%~1"=="" if /i not "%~1"=="--no-path" set "TARGET=%~1"
if /i "%~1"=="--no-path" set "ADD_PATH=no"
if /i "%~2"=="--no-path" set "ADD_PATH=no"

if not exist "%~dp0jamo.exe" (
    echo I can't find jamo.exe next to this file. Unzip the whole folder first, then run install.cmd from inside it.
    goto finish_failed
)
if not exist "%TARGET%" mkdir "%TARGET%" 2>nul
copy /y "%~dp0jamo.exe" "%TARGET%\jamo.exe" >nul
if errorlevel 1 (
    echo I couldn't copy jamo.exe into %TARGET%. If Jamo is running, close it and try again.
    goto finish_failed
)

if "%ADD_PATH%"=="yes" (
    rem The folder goes through an environment variable, so a name with spaces or an apostrophe can't break the command
    set "JAMO_TARGET=%TARGET%"
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$t = $env:JAMO_TARGET; $p = [Environment]::GetEnvironmentVariable('Path', 'User'); if (-not $p) { $p = '' }; if (($p -split ';') -notcontains $t) { [Environment]::SetEnvironmentVariable('Path', ($p.TrimEnd(';') + ';' + $t).TrimStart(';'), 'User') }"
    if errorlevel 1 (
        echo Jamo is in %TARGET%, but I couldn't add it to your PATH. You can add that folder to your PATH yourself.
        goto finish_failed
    )
)

echo Jamo is installed in %TARGET%.
if "%ADD_PATH%"=="yes" echo Open a new terminal, then try: jamo version
if not defined ASKED pause
exit /b 0

:finish_failed
if not defined ASKED pause
exit /b 1
