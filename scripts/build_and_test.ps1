# Build the C++ simulator executable and the simulation Python module, then run the Python smoke test.
# Run from the project root (where store.yaml and this script's parent directory live).
# Usage: .\scripts\build_and_test.ps1

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
Set-Location $ProjectRoot

# ── Resolve the Python interpreter ───────────────────────────────────────────
# The simulation.pyd is compiled against a specific Python ABI.
# If the build directory already contains a .pyd, honour its ABI tag (e.g. cp311).
# Otherwise fall back to the first Python 3.x found via py launcher or PATH.

function Get-PydPythonVersion {
    $pyds = Get-ChildItem -Path "build" -Filter "simulation.cp*.pyd" -ErrorAction SilentlyContinue
    if ($pyds) {
        # Extract "311" from "simulation.cp311-win_amd64.pyd"
        if ($pyds[0].Name -match 'simulation\.cp(\d+)-') {
            return $Matches[1]
        }
    }
    return $null
}

$PythonExe = $null
$abiTag = Get-PydPythonVersion

# Helper: ask a specific py-launcher version for its executable path.
# Uses a temp .py file to avoid PowerShell 5's broken argument quoting for
# native programs (semicolons and parens get parsed as PS syntax).
function Get-PyExePath {
    param([string]$Version)
    $tmp = [System.IO.Path]::GetTempPath() + "get_pyexe_$Version.py"
    "import sys" | Set-Content $tmp -Encoding UTF8
    "print(sys.executable)" | Add-Content $tmp -Encoding UTF8
    $path = & py "-$Version" $tmp 2>$null
    Remove-Item $tmp -ErrorAction SilentlyContinue
    if ($LASTEXITCODE -eq 0 -and $path) { return $path.Trim() }
    return $null
}

if ($abiTag) {
    $major = $abiTag[0]
    $minor = $abiTag.Substring(1)
    Write-Host "Detected existing pyd built for Python $major.$minor — using that interpreter."
    $PythonExe = Get-PyExePath "$major.$minor"
}

if (-not $PythonExe) {
    # No existing pyd or launcher lookup failed — use the first py 3.x available.
    foreach ($ver in @("3.11","3.12","3.13","3.10")) {
        $candidate = Get-PyExePath $ver
        if ($candidate) {
            $PythonExe = $candidate
            Write-Host "Selected Python ${ver}: $PythonExe"
            break
        }
    }
}

if (-not $PythonExe) {
    # Last resort: whatever 'python' resolves to.
    $PythonExe = (Get-Command python -ErrorAction Stop).Source
    Write-Host "Falling back to system python: $PythonExe"
}

Write-Host "Using Python: $PythonExe"

# ── CMake configure & build ───────────────────────────────────────────────────
$BuildDir = "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}
Set-Location $BuildDir

Write-Host "Configuring with CMake..."
cmake ../cxx "-DPython_EXECUTABLE=$PythonExe" "-DPYTHON_EXECUTABLE=$PythonExe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building..."
cmake --build . --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Set-Location $ProjectRoot

# ── PYTHONPATH ────────────────────────────────────────────────────────────────
$env:PYTHONPATH = "$BuildDir;$BuildDir\Release$(if ($env:PYTHONPATH) { ";$env:PYTHONPATH" })"

# ── Copy MinGW runtime DLLs next to the .pyd ─────────────────────────────────
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

# ── Smoke test ────────────────────────────────────────────────────────────────
Write-Host "Running Python smoke test with $PythonExe..."
& $PythonExe tests/test_simulation.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Done: simulator and simulation module built, smoke test passed."
