@echo off
cd /d "%~dp0"
if not exist bin\minidb.exe (
  echo Building current MiniDB source...
  powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
  if errorlevel 1 goto failed
)
set "MINIDB_DEMO_FILE=data/demo-%RANDOM%-%RANDOM%.db"
bin\minidb.exe --db "%MINIDB_DEMO_FILE%" --script examples/demo.sql
if errorlevel 1 goto failed
echo.
echo === Restart and verify persistence ===
bin\minidb.exe --db "%MINIDB_DEMO_FILE%" --script examples/restart.sql
if errorlevel 1 goto failed
echo.
echo Demo completed. Database: %MINIDB_DEMO_FILE%
pause
exit /b 0
:failed
echo Demo failed.
pause
exit /b 1
