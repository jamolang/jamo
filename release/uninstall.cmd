@echo off
rem Removes Jamo for this Windows user: deletes its folder and takes it off your PATH. Run: uninstall.cmd [folder] [--no-path]
setlocal
rem Double-clicking passes no inputs. Only then does it wait for a key at the end, so the window stays open to be read
set "ASKED=%~1"
set "TARGET=%LOCALAPPDATA%\Programs\Jamo"
set "FROM_PATH=yes"
if not "%~1"=="" if /i not "%~1"=="--no-path" set "TARGET=%~1"
if /i "%~1"=="--no-path" set "FROM_PATH=no"
if /i "%~2"=="--no-path" set "FROM_PATH=no"

if exist "%TARGET%\jamo.exe" del /q "%TARGET%\jamo.exe"
if exist "%TARGET%\jamo.exe" (
    echo I couldn't delete %TARGET%\jamo.exe. If Jamo is running, close it and try again.
    goto finish_failed
)
rmdir "%TARGET%" 2>nul

if "%FROM_PATH%"=="yes" (
    set "JAMO_TARGET=%TARGET%"
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$t = $env:JAMO_TARGET; $p = [Environment]::GetEnvironmentVariable('Path', 'User'); if ($p) { [Environment]::SetEnvironmentVariable('Path', (($p -split ';') | Where-Object { $_ -and $_ -ne $t }) -join ';', 'User') }"
)

echo Jamo is uninstalled. Your programs and .jamo files are still where you left them.
if not defined ASKED pause
exit /b 0

:finish_failed
if not defined ASKED pause
exit /b 1
