@echo off
cd /d "%~dp0"
if not exist bin\minidb.exe (
  echo Building current MiniDB source...
  powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1
  if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
  )
)
bin\minidb.exe
pause
