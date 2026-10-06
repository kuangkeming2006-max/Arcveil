@echo off
setlocal
cd /d "%~dp0"
if not exist diagnostics mkdir diagnostics
echo Arcveil v56.2: live Microsoft Pinyin / split-thread WGL test
echo Keep focus on the test window until the test finishes.
McOverlayImeLiveTests.exe --split --output "%~dp0diagnostics" %* > "diagnostics\ime-live.log" 2>&1
set "testExit=%errorlevel%"
type "diagnostics\ime-live.log"
echo.
echo Test exit code: %testExit% (0 = passed)
echo Screenshot and transcript: %~dp0diagnostics
pause
exit /b %testExit%
