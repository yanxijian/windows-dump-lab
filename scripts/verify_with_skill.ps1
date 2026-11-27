#Requires -Version 5.1
<#
.SYNOPSIS
    Verify the captured dumps against expected_answers.json by driving the
    windows-dump-analysis skill's own cdb-chain.ps1.

.DESCRIPTION
    For every crash dump this runs `-Chain sources` (which resolves the faulting
    stack to file:line) and looks for the expected source file with a line number
    within -Tolerance. Watchdog-only cases (15, 21-30) have no exception record,
    so they are checked for existence and non-zero size instead.

    The skill is looked up as a sibling checkout (..\windows-dump-analysis);
    pass -SkillDir when the two repositories are not siblings.

.PARAMETER SkillDir
    Path to the windows-dump-analysis checkout.

.PARAMETER Cases
    Subset of case ids, e.g. -Cases 1,9,17.

.PARAMETER Tolerance
    Accepted distance in lines between the reported and the expected line.

.EXAMPLE
    powershell -NoProfile -File scripts\verify_with_skill.ps1

.EXAMPLE
    powershell -NoProfile -File scripts\verify_with_skill.ps1 -Cases 9,17 -SkillDir D:\src\windows-dump-analysis
#>
[CmdletBinding()]
param(
    [string]$SkillDir,
    [string[]]$Cases = @(),
    [int]$Tolerance = 3
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

$caseIds = @()
foreach ($item in $Cases) {
    foreach ($part in ($item -split ',')) {
        $trimmed = $part.Trim()
        if ($trimmed) { $caseIds += [int]$trimmed }
    }
}

# ------------------------------------------------------------------ locate skill
if (-not $SkillDir) {
    $SkillDir = Join-Path (Split-Path -Parent $repo) 'windows-dump-analysis'
}
$chainScript = Join-Path $SkillDir 'scripts\cdb-chain.ps1'
if (-not (Test-Path -LiteralPath $chainScript)) {
    throw "cdb-chain.ps1 not found under '$SkillDir'. Clone windows-dump-analysis as a sibling or pass -SkillDir."
}
Write-Output ("skill : {0}" -f $SkillDir)

# ------------------------------------------------------------------ load answers
$answersPath = Join-Path $repo 'expected_answers.json'
if (-not (Test-Path -LiteralPath $answersPath)) { throw "missing $answersPath" }
$answers = Get-Content -LiteralPath $answersPath -Raw | ConvertFrom-Json
$byId = @{}
foreach ($case in $answers.cases) { $byId[[int]$case.case_id] = $case }

if ($caseIds.Count -eq 0) { $caseIds = @($byId.Keys | Sort-Object) }

# cases whose dump comes from the watchdog and therefore has no exception record
$watchdogOnly = @(15, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30)

$dumpDir = Join-Path $repo 'output\dumps'
$binDir = Join-Path $repo 'output\bin'
$cache = Join-Path $env:LOCALAPPDATA 'windows-dump-analysis\symbols'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$sym = $binDir + ';SRV*' + $cache + '*https://msdl.microsoft.com/download/symbols'

$results = @()
foreach ($id in $caseIds) {
    $case = $byId[$id]
    if (-not $case) {
        $results += [pscustomobject]@{ Case = $id; Name = '(unknown)'; Status = 'FAIL'; Detail = 'no entry in expected_answers.json' }
        continue
    }

    $dump = Join-Path $dumpDir $case.dump_file
    if (-not (Test-Path -LiteralPath $dump)) {
        $results += [pscustomobject]@{ Case = $id; Name = $case.case_name; Status = 'FAIL'; Detail = 'dump missing' }
        continue
    }
    if ((Get-Item $dump).Length -eq 0) {
        $results += [pscustomobject]@{ Case = $id; Name = $case.case_name; Status = 'FAIL'; Detail = 'dump is 0 bytes' }
        continue
    }

    if ($watchdogOnly -contains $id) {
        $results += [pscustomobject]@{ Case = $id; Name = $case.case_name; Status = 'PASS'; Detail = 'watchdog dump present' }
        continue
    }

    $output = (@(& powershell -NoProfile -ExecutionPolicy Bypass -File $chainScript `
                -Dump $dump -Sym $sym -Chain sources 2>&1) -join "`n")

    $expectedName = Split-Path -Leaf ($case.expected_source_file -replace '/', '\')
    $expectedLine = [int]$case.expected_line_approx

    $matched = $false
    $detail = 'no source line in dump output'
    foreach ($m in [regex]::Matches($output, '([A-Za-z]:\\[^:\r\n]+\.(?:cpp|cxx|cc|h|hpp)):(\d+)')) {
        # two capture groups: the whole path, then the line number
        $file = Split-Path -Leaf $m.Groups[1].Value
        $line = [int]$m.Groups[2].Value
        if ($file -eq $expectedName -and [Math]::Abs($line - $expectedLine) -le $Tolerance) {
            $matched = $true
            $detail = ('{0}:{1} (expected {2})' -f $file, $line, $expectedLine)
            break
        }
    }

    $results += [pscustomobject]@{
        Case   = $id
        Name   = $case.case_name
        Status = if ($matched) { 'PASS' } else { 'FAIL' }
        Detail = $detail
    }
}

# ------------------------------------------------------------------ report
$results |
    Format-Table Case, Name, Status, Detail -AutoSize |
    Out-String -Width 200 |
    Write-Output

$ok = @($results | Where-Object Status -eq 'PASS').Count
$total = $results.Count
Write-Output ("verified: {0}/{1}" -f $ok, $total)
if ($ok -lt $total) { exit 1 }
exit 0
