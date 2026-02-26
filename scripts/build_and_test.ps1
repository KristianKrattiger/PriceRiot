# Build the C++ simulator executable and the simulation Python module, then run the Python smoke test.
# Run from the project root (where store.yaml and this script's parent directory live).
# Usage: .\scripts\build_and_test.ps1

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
Set-Location $ProjectRoot

$BuildDir = "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}
Set-Location $BuildDir

Write-Host "Configuring with CMake..."
cmake ../cxx
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building Release..."
cmake --build . --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Set-Location $ProjectRoot

# Python module may be in build/ or build/Release depending on CMake generator
$env:PYTHONPATH = "$BuildDir;$BuildDir\Release$(if ($env:PYTHONPATH) { ";$env:PYTHONPATH" })"

# On Windows with MinGW, the .pyd needs MinGW runtime DLLs. Copy them next to the .pyd
# so the loader finds them (same dir as the module takes precedence).
$cachePath = Join-Path $ProjectRoot (Join-Path $BuildDir "CMakeCache.txt")
$mingwBin = $null
if (Test-Path $cachePath) {
    $line = Get-Content $cachePath | Select-String -Pattern "^CMAKE_CXX_COMPILER:FILEPATH=" | Select-Object -First 1
    if ($line) {
        $compilerPath = ($line.Line -split "=", 2)[1].Trim()
        $mingwBin = Split-Path -Parent $compilerPath
        if (Test-Path $mingwBin) {
            $env:PATH = "$mingwBin;$env:PATH"
        }
    }
}
$runtimeDlls = @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
$pydDirs = @(Join-Path $ProjectRoot $BuildDir)
if (Test-Path (Join-Path $ProjectRoot "$BuildDir\Release")) {
    $pydDirs += Join-Path $ProjectRoot "$BuildDir\Release"
}
if ($mingwBin) {
    foreach ($d in $pydDirs) {
        foreach ($dll in $runtimeDlls) {
            $src = Join-Path $mingwBin $dll
            if (Test-Path $src) {
                Copy-Item -Path $src -Destination $d -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

Write-Host "Running Python smoke test..."
python tests/test_simulation.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Done: simulator and simulation module built, smoke test passed."
