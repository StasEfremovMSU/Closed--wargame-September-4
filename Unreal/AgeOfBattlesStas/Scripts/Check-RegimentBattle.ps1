param(
    [string]$EngineRoot = 'D:/EpicGameLauncher/UE_5.8',
    [int]$TimeoutSeconds = 120,
    [ValidateSet('River', 'OpenField', 'StoneWalls')][string]$Terrain = 'River',
    [switch]$Mission
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$project = Join-Path $projectRoot 'AgeOfBattlesStas.uproject'
$map = Join-Path $projectRoot 'Content/Battle/Maps/RegimentField.umap'
if (!(Test-Path -LiteralPath $map)) {
    throw 'RegimentField level has not been built.'
}

$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$log = Join-Path $projectRoot ('Saved/Logs/RegimentBattleSmoke-' + [guid]::NewGuid().ToString('N') + '.log')
$mode = if ($Mission) { '-RegimentMissionSmoke' } else { '-RegimentBattleSmoke' }
$arguments = @(
    ('"' + $project + '"'), '/Game/Battle/Maps/RegimentField', '-game',
    '-nullrhi', '-nosound', '-unattended', '-noLiveCoding', $mode,
    ('-abslog="' + $log + '"')
)
if ($Mission) {
    $arguments += "-RegimentMissionTerrain=$Terrain"
}
$run = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
try {
    if (!$run.WaitForExit($TimeoutSeconds * 1000)) {
        throw "Battle smoke run timed out. Log: $log"
    }
    $raw = Get-Content -Raw -LiteralPath $log
    if ($Mission) {
        $layout = switch ($Terrain) {
            'River' { 'bridges=2 fords=1 terrain=River walls=0 gates=0' }
            'OpenField' { 'bridges=0 fords=0 terrain=OpenField walls=0 gates=0' }
            'StoneWalls' { 'bridges=0 fords=0 terrain=StoneWalls walls=4 gates=3' }
        }
        $flankRoutes = if ($Terrain -eq 'OpenField') { '[1-9]\d*' } else { '\d+' }
        $checkpoints = @(
            ('RegimentMission setup: regiments=12 blue=6 orange=6 roster=462 ' + $layout),
            'RegimentMission sighted: flags=6 revised=1 preserved=5',
            'RegimentMission encounter: first=[1-6] second=(?:[789]|1[012])',
            'RegimentMission tactical: regiments=12 figures=470 mass=470 blue=236 orange=234 commands=20',
            'RegimentMission staff: officers=12 banners=12 musicians=12 leaders=6 generals=2',
            'RegimentMission mounted staff: officers=2 banners=2 musicians=2 leaders=2 generals=2',
            'RegimentMission troops: spears=48 mounted=104',
            'RegimentMission decisions: generals=2 groups=6 leaves=12 active=[1-9]\d* moved=[1-9]\d* screens=[1-9]\d* daggers=[1-9]\d* contact=[1-9]\d*',
            ('RegimentMission army orders: plans=2 groups=6 flankRoutes=' + $flankRoutes),
            'RegimentMission decision display: own=10 opposing=0',
            'RegimentMission skirmisher orders: recalled=1 deployed=1 outside=[1-9]\d*',
            'RegimentMission advanced: tick=\d+ aligned=1 action=1'
        )
        $previous = -1
        foreach ($pattern in $checkpoints) {
            $checkpoint = [regex]::Match($raw, $pattern)
            if (!$checkpoint.Success -or $checkpoint.Index -le $previous) {
                throw "Mission did not reach checkpoint '$pattern'. Exit: $($run.ExitCode). Log: $log"
            }
            $previous = $checkpoint.Index
            $checkpoint.Value
        }
        if ($run.ExitCode -ne 0) {
            throw "Mission reported a runtime failure. Exit: $($run.ExitCode). Log: $log"
        }
        "Verified $Terrain, twelve regiments, waypoint replacement, BattleCore handoff, army orders, staff, and skirmisher orders. Log: $log"
        return
    }
    $start = [regex]::Match($raw, 'RegimentBattle ready: units=100 entities=100 meshes=100 commands=8 poses=100 x1=(-?[\d.]+) x51=(-?[\d.]+) y1=(-?[\d.]+) y51=(-?[\d.]+) yaw1=(-?[\d.]+) yaw51=(-?[\d.]+)')
    $finish = [regex]::Match($raw, 'RegimentBattle advanced: tick=100 units=100 entities=100 meshes=100 poses=100 stable=1 x1=(-?[\d.]+) x51=(-?[\d.]+)')
    $figures = [regex]::Match($raw, 'RegimentBattle figures: rigs=3 equipped=100 painted=100 arrowSlots=[1-9]\d*')
    $narrow = [regex]::Match($raw, 'RegimentBattle opening: phase=column coreTick=0 width1=4\.40 width2=4\.40')
    $ready = [regex]::Match($raw, 'RegimentBattle opening: phase=ready coreTick=0 width1=9\.90 width2=9\.90')
    $combat = [regex]::Match($raw, 'RegimentBattle combat: meleeHits=[1-9]\d* reactions=[1-9]\d* meleeDeaths=[1-9]\d* collapsed=1 expired=1 aligned=1(?: firstMeleeOnFlank=([01]))?')
    if ($run.ExitCode -ne 0 -or !$start.Success -or !$finish.Success -or !$figures.Success -or
        !$narrow.Success -or !$ready.Success -or $narrow.Index -le $start.Index -or
        $ready.Index -le $narrow.Index -or $finish.Index -le $ready.Index) {
        throw "UE did not complete the formation exercise and subsequent battle. Exit: $($run.ExitCode). Log: $log"
    }
    if (!$combat.Success -or $combat.Index -le $finish.Index) {
        throw "UE did not show the melee hit and fall on their targets, then clear the fallen figure. Log: $log"
    }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $x1 = [double]::Parse($start.Groups[1].Value, $culture)
    $x51 = [double]::Parse($start.Groups[2].Value, $culture)
    $y1 = [double]::Parse($start.Groups[3].Value, $culture)
    $y51 = [double]::Parse($start.Groups[4].Value, $culture)
    $yaw1 = [double]::Parse($start.Groups[5].Value, $culture)
    $yaw51 = [double]::Parse($start.Groups[6].Value, $culture)
    $x1Later = [double]::Parse($finish.Groups[1].Value, $culture)
    $x51Later = [double]::Parse($finish.Groups[2].Value, $culture)
    if ([math]::Abs($x1 + 1480) -gt 1 -or [math]::Abs($x51 - 1615.61) -gt 1 -or
        [math]::Abs($y1 + 495) -gt 1 -or [math]::Abs($y51 - 535.07) -gt 1 -or
        [math]::Abs($yaw1) -gt 0.1 -or [math]::Abs($yaw51 - 165) -gt 0.1) {
        throw "Unit transforms did not start in the expected oblique formation. Log: $log"
    }
    if ($x1Later - $x1 -lt 100 -or $x51 - $x51Later -lt 100) {
        throw "Units did not advance toward each other. Log: $log"
    }
    if ($combat.Groups[1].Value -ne '1') {
        throw "The first melee attacks were not confined to the leading flank. Log: $log"
    }
    $start.Value
    $narrow.Value
    $ready.Value
    $finish.Value
    $figures.Value
    $combat.Value
    "Verified formation, Mass movement, oblique contact and melee presentation. Log: $log"
} finally {
    if (!$run.HasExited) { Stop-Process -Id $run.Id }
}
