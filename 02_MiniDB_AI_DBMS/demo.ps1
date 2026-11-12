$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path -LiteralPath 'bin/minidb.exe')) { & './build.ps1' }
    $demoFile = 'data/demo-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.db'
    & './bin/minidb.exe' '--db' $demoFile '--script' 'examples/demo.sql'
    if ($LASTEXITCODE -ne 0) { throw 'Demo failed' }
    Write-Host "Restarting MiniDB to verify persistence: $demoFile"
    & './bin/minidb.exe' '--db' $demoFile '--script' 'examples/restart.sql'
    if ($LASTEXITCODE -ne 0) { throw 'Restart verification failed' }
} finally { Pop-Location }
