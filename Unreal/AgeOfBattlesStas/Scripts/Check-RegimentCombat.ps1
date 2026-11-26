[CmdletBinding()]
param(
    [string]$EngineRoot = 'D:/EpicGameLauncher/UE_5.8',
    [ValidatePattern('^[a-z0-9_]+$')][string]$Era,
    [ValidateRange(0, 2147483647)][int]$Seed = 42,
    [ValidateRange(1, 600)][int]$MaxSeconds = 180,
    [ValidateRange(30, 3600)][int]$TimeoutSeconds = 600,
    [switch]$Strict,
    [switch]$IncludeLarge,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$project = Join-Path $projectRoot 'AgeOfBattlesStas.uproject'
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$build = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
if (!(Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "Unreal Editor was not found at $editor. Pass -EngineRoot with the Unreal installation directory."
}
if (!$SkipBuild) {
    & $build AgeOfBattlesStasEditor Win64 Development "-Project=$project" -Module=RegimentBattleRuntime -WaitMutex -NoHotReloadFromIDE -NoLiveCoding
    if ($LASTEXITCODE -ne 0) { throw 'Unreal Editor build failed.' }
}

$reportDirectory = Join-Path $projectRoot 'Saved/Reports'
New-Item -Path $reportDirectory -ItemType Directory -Force | Out-Null
$scope = if ($Era) { $Era } else { 'all' }
$runId = '{0}-{1}-{2}' -f $scope, (Get-Date -Format 'yyyyMMdd-HHmmss'), [guid]::NewGuid().ToString('N').Substring(0, 8)
$report = Join-Path $reportDirectory "CombatAudit-$runId.md"
$log = Join-Path $projectRoot "Saved/Logs/CombatAudit-$runId.log"
$arguments = @(
    ('"' + $project + '"'), '-NullRHI', '-Unattended', '-NoSound', '-NoSplash', '-NoLiveCoding',
    '-ExecCmds="Automation RunTests RegimentCombatAudit.Scenarios"',
    '-TestExit="Automation Test Queue Empty"',
    ('-abslog="' + $log + '"'),
    ('-RegimentAuditReport="' + $report + '"'),
    "-RegimentAuditSeed=$Seed", "-RegimentAuditMaxSeconds=$MaxSeconds"
)
if ($Era) { $arguments += "-RegimentAuditEra=$Era" }
if ($Strict) { $arguments += '-RegimentAuditStrict' }
if ($IncludeLarge) { $arguments += '-RegimentAuditIncludeLarge' }

$run = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
try {
    if (!$run.WaitForExit($TimeoutSeconds * 1000)) {
        throw "Combat audit timed out. Log: $log"
    }
    if (!(Test-Path -LiteralPath $report -PathType Leaf)) {
        throw "Combat audit did not write a report. Editor exit: $($run.ExitCode). Log: $log"
    }
    "Report: $report"
    $completed = Select-String -LiteralPath $log -Pattern 'Test Completed\. Result=\{Success\}.*Path=\{RegimentCombatAudit\.Scenarios\}' -Quiet
    if ($run.ExitCode -ne 0 -or !$completed) {
        throw "Combat audit failed. See $report and $log"
    }
} finally {
    if (!$run.HasExited) { Stop-Process -Id $run.Id }
}
