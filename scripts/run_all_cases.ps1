#Requires -Version 5.1
<#
.SYNOPSIS
    Build windows-dump-lab and run all 30 fault cases, producing one dump per case.

.DESCRIPTION
    Configures and builds harness/ with CMake + Ninja inside the MSVC x64
    environment, then runs CrashHarness.exe --case N for every scenario.
    Each case is allowed 8 seconds; the leak cases (25-30) get more because they
    write full-memory dumps with handle data (case 25 alone commits 200 MB).
    A final table reports the dump size, elapsed time and success rate.

.PARAMETER Cases
    Subset of case ids to run. Default: 1..30.

.PARAMETER SkipBuild
    Reuse an existing output/bin/CrashHarness.exe.

.PARAMETER QtDir
    Qt installation prefix. Default: D:\Codes\Qt6.8.4, then $env:QTDIR.

.PARAMETER KeepDumps
    Do not delete existing dumps in output/dumps before the run.

.PARAMETER TimeoutSec
    Per-case timeout in seconds. Default 8.

.PARAMETER LeakTimeoutSec
    Per-case timeout for the leak scenarios 25-30. Default 30.

.EXAMPLE
    powershell -NoProfile -File scripts\run_all_cases.ps1

.EXAMPLE
    powershell -NoProfile -File scripts\run_all_cases.ps1 -Cases 1,2,3 -SkipBuild
#>
[CmdletBinding()]
param(
    [string[]]$Cases = @(),
    [switch]$SkipBuild,
    [string]$QtDir,
    [switch]$KeepDumps,
    [int]$TimeoutSec = 8,
    [int]$LeakTimeoutSec = 30
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$harnessDir = Join-Path $repoRoot 'harness'
$buildDir = Join-Path $repoRoot 'build\lab'
$outputDir = Join-Path $repoRoot 'output'
$binDir = Join-Path $outputDir 'bin'
$dumpDir = Join-Path $outputDir 'dumps'
$exe = Join-Path $binDir 'CrashHarness.exe'

function Find-VcVars {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -property installationPath
        if ($installPath) {
            $candidate = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    foreach ($base in @('C:\Program Files\Microsoft Visual Studio', 'C:\Program Files (x86)\Microsoft Visual Studio')) {
        if (-not (Test-Path -LiteralPath $base)) { continue }
        $hit = Get-ChildItem -LiteralPath $base -Recurse -Filter 'vcvars64.bat' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    throw 'vcvars64.bat not found: install the MSVC x64 toolchain.'
}

function Resolve-QtDir {
    param([string]$Explicit)
    if ($Explicit) {
        if (-not (Test-Path -LiteralPath (Join-Path $Explicit 'lib\cmake\Qt6\Qt6Config.cmake'))) {
            throw "Qt6Config.cmake not found under $Explicit"
        }
        return $Explicit
    }
    foreach ($candidate in @('D:\Codes\Qt6.8.4', $env:QTDIR, 'C:\Qt\6.8.4\msvc2022_64')) {
        if (-not $candidate) { continue }
        if (Test-Path -LiteralPath (Join-Path $candidate 'lib\cmake\Qt6\Qt6Config.cmake')) { return $candidate }
    }
    throw 'Qt 6 not found. Pass -QtDir <prefix> (must contain lib\cmake\Qt6\Qt6Config.cmake).'
}

function Build-Lab {
    param([string]$VcVars, [string]$Qt)
    $bat = Join-Path $buildDir 'build.bat'
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    @(
        '@echo off'
        ('call "{0}" >nul 2>&1' -f $VcVars)
        ('cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PREFIX_PATH="{2}"' -f $harnessDir, $buildDir, $Qt)
        'if errorlevel 1 exit /b 1'
        ('cmake --build "{0}" --config RelWithDebInfo' -f $buildDir)
        'exit /b %ERRORLEVEL%'
    ) | Set-Content -LiteralPath $bat -Encoding ASCII

    & cmd /c $bat | ForEach-Object { Write-Output ("  " + $_) }
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure/build failed (see output above)' }
    if (-not (Test-Path -LiteralPath $exe)) { throw "build succeeded but $exe is missing" }
}

# ------------------------------------------------------------------ build
if (-not $SkipBuild) {
    $vcvars = Find-VcVars
    $qt = Resolve-QtDir -Explicit $QtDir
    Write-Output ("vcvars: {0}" -f $vcvars)
    Write-Output ("qt    : {0}" -f $qt)
    Write-Output 'building ...'
    Build-Lab -VcVars $vcvars -Qt $qt
}
if (-not (Test-Path -LiteralPath $exe)) { throw "missing $exe (run without -SkipBuild)" }

# ------------------------------------------------------- case list + dumps dir
New-Item -ItemType Directory -Force -Path $dumpDir | Out-Null
if (-not $KeepDumps) {
    Get-ChildItem -LiteralPath $dumpDir -Filter '*.dmp' -ErrorAction SilentlyContinue | Remove-Item -Force
}

$catalog = @{}
foreach ($line in @(& $exe --list)) {
    if ($line -match '^\s*(\d+)\s+(\S+)\s+(qt|native)\s*$') {
        $catalog[[int]$Matches[1]] = $Matches[2]
    }
}
if ($catalog.Count -ne 30) { throw ("case catalog looks wrong: {0} entries" -f $catalog.Count) }

# -Cases accepts both "9,10,12" (one argument, as -File passes it) and 9 10 12
$caseIds = @()
foreach ($item in $Cases) {
    foreach ($part in ($item -split ',')) {
        $trimmed = $part.Trim()
        if ($trimmed) { $caseIds += [int]$trimmed }
    }
}
if ($caseIds.Count -eq 0) { $caseIds = 1..30 }

# ------------------------------------------------------------------ run cases
$results = @()
foreach ($id in $caseIds) {
    $name = $catalog[$id]
    if (-not $name) { continue }
    $dumpFile = Join-Path $dumpDir ($name + '.dmp')
    $timeout = if ($id -ge 25) { $LeakTimeoutSec } else { $TimeoutSec }

    $watch = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $exe -ArgumentList '--case', $id -WorkingDirectory $repoRoot -PassThru -WindowStyle Hidden
    $exited = $proc.WaitForExit($timeout * 1000)
    if (-not $exited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        $proc.WaitForExit(3000) | Out-Null
    }
    $watch.Stop()

    $sizeMb = 0
    $status = 'MISSING'
    if (Test-Path -LiteralPath $dumpFile) {
        $bytes = (Get-Item $dumpFile).Length
        $sizeMb = [Math]::Round($bytes / 1MB, 1)
        if ($bytes -eq 0) {
            $status = 'EMPTY'
        }
        elseif ($exited) {
            $status = 'ok'
        }
        else {
            $status = 'timeout'
        }
    }

    $results += [pscustomobject]@{
        Case    = $id
        Name    = $name
        Status  = $status
        DumpMB  = $sizeMb
        Elapsed = [Math]::Round($watch.Elapsed.TotalSeconds, 1)
        Timeout = $timeout
    }
    Write-Output ("[{0,2}] {1,-36} {2,-8} {3,7:N1} MB  {4,5:N1}s" -f $id, $name, $status, $sizeMb, $watch.Elapsed.TotalSeconds)
}

# ------------------------------------------------------------------ summary
$ok = @($results | Where-Object Status -eq 'ok').Count
$total = $results.Count
$rate = if ($total -gt 0) { [Math]::Round(100.0 * $ok / $total, 1) } else { 0 }

Write-Output ''
$results |
    Format-Table Case, Name, Status, DumpMB, Elapsed, Timeout -AutoSize |
    Out-String -Width 200 |
    Write-Output

Write-Output ("dumps : {0}" -f $dumpDir)
Write-Output ("result: {0}/{1} ({2}%)" -f $ok, $total, $rate)
if ($ok -lt $total) {
    Write-Output 'failed cases:'
    $results | Where-Object Status -ne 'ok' | ForEach-Object { Write-Output ("  {0}  {1}" -f $_.Case, $_.Name) }
    exit 1
}
exit 0
