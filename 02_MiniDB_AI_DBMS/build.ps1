param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force -Path 'bin' | Out-Null
    & $Compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-static' '-Iinclude' 'src/main.cpp' 'src/sql.cpp' 'src/model.cpp' 'src/storage.cpp' 'src/engine.cpp' 'src/bptree.cpp' 'src/page_buffer.cpp' 'src/lock_manager.cpp' '-o' 'bin/minidb.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    Write-Host 'Built bin/minidb.exe. Run .\bin\minidb.exe or .\demo.ps1'
} finally { Pop-Location }
