#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Game,
    [Parameter(Mandatory)][string] $Output,
    [string] $FixtureDirectory,
    [ValidateSet('Cannae', 'Cannae10k', 'Waterloo')]
    [string[]] $Workload = @('Cannae', 'Cannae10k', 'Waterloo'),
    [ValidateRange(180, 10000)][int] $Ticks = 400,
    [ValidateRange(30, 1800)][int] $TimeoutSeconds = 300,
    [ValidateRange(1, 10)][int] $Repeats = 1,
    [switch] $Legacy,
    [switch] $ComparePresentation
)

$ErrorActionPreference = 'Stop'
$gamePath = (Resolve-Path -LiteralPath $Game).Path
if ($gamePath -notlike '*-Win64-Shipping.exe') {
    throw 'Pass the actual Win64 Shipping executable from the package Binaries folder.'
}
if ($Legacy -and ($FixtureDirectory -or $ComparePresentation)) {
    throw 'Legacy packages cannot load an external fixture or toggle the new presentation change.'
}
$fixtures = if ($FixtureDirectory) { (Resolve-Path -LiteralPath $FixtureDirectory).Path }
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
if (Test-Path -LiteralPath $outputPath) {
    throw "Choose a new output directory. $outputPath already exists."
}
New-Item -ItemType Directory -Path $outputPath | Out-Null

$manifest = [ordered]@{
    executable = $gamePath
    executable_sha256 = (Get-FileHash -LiteralPath $gamePath).Hash
    machine = (Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name)
    gpu = (Get-CimInstance Win32_VideoController | Select-Object -ExpandProperty Name)
    utc_started = [DateTime]::UtcNow.ToString('o')
    width = 1152
    height = 740
    tick_count = $Ticks
    repeats = $Repeats
    legacy = [bool]$Legacy
    compare_presentation = [bool]$ComparePresentation
    fixture_directory = $fixtures
    fixture_sha256 = if ($fixtures) {
        Get-ChildItem -LiteralPath $fixtures -Filter '*.json' -File |
            Get-FileHash | Select-Object Path, Hash
    }
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'manifest.json') -Encoding utf8

function Read-Result([string] $Line) {
    $result = @{}
    foreach ($part in ($Line -split ' ' | Select-Object -Skip 1)) {
        $key, $value = $part -split '=', 2
        if ($value) { $result.Add($key, $value) }
    }
    return $result
}

function Test-SampledWindow($Window) {
    return $Window -and [int]$Window.steps -gt 0 -and [int]$Window.frames -gt 0 -and
        [int]$Window.present_frames -gt 0
}

$signatures = @{}
$rows = foreach ($iteration in 1..$Repeats) {
    foreach ($scenario in $Workload) {
        $expectedUnits = if ($scenario -eq 'Cannae10k') { 10000 } else { 5000 }
        $versions = if ($ComparePresentation) { @('previous', 'current') } else { @('current') }
        foreach ($presentation in $versions) {
            foreach ($camera in @('overview', 'officer')) {
                $mode = if ($camera -eq 'officer') { 'possessed' } else { 'overview' }
                $file = Join-Path $outputPath "$iteration-$scenario-$presentation-$camera.txt"
                $switches = @('-windowed', '-ResX=1152', '-ResY=740', '-ForceRes', '-NoVSync', '-nosound',
                    "-RegimentPerfPreset=$scenario", '-RegimentPerfReport', "-RegimentPerfTicks=$Ticks",
                    ('-RegimentPerfOutput="{0}"' -f $file))
                if ($Legacy) {
                    if ($mode -eq 'possessed') { $switches += '-RegimentPerfOfficer' }
                } else {
                    $switches += "-RegimentPerfCamera=$camera"
                }
                if ($presentation -eq 'previous') { $switches += '-RegimentPerfBaselinePresentation' }
                if ($fixtures) { $switches += ('-RegimentPerfPresetDirectory="{0}"' -f $fixtures) }

                Write-Host "Measuring $scenario with $camera camera ($presentation, run $iteration)"
                $start = [Diagnostics.ProcessStartInfo]::new($gamePath)
                $start.WorkingDirectory = Split-Path -Parent $gamePath
                # Unreal reads the raw command line and requires quotes after the equals sign.
                $start.Arguments = $switches -join ' '
                $gameProcess = [Diagnostics.Process]::Start($start)
                $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
                $sample = $null
                try {
                    do {
                        Start-Sleep -Milliseconds 300
                        if (Test-Path -LiteralPath $file) {
                            $sample = Get-Content -LiteralPath $file | Where-Object {
                                $_ -like "RegimentPerf mode=$mode *complete=1 steps=$Ticks *"
                            } | Select-Object -First 1
                        }
                    } until ($sample -or $gameProcess.HasExited -or [DateTime]::UtcNow -ge $deadline)
                    if (!$sample) {
                        throw "No completed $Ticks-tick sample in $file."
                    }
                } finally {
                    if (!$gameProcess.HasExited) {
                        Stop-Process -InputObject $gameProcess
                        $null = $gameProcess.WaitForExit(5000)
                    }
                    $gameProcess.Dispose()
                }

                $statistics = Read-Result $sample
                if ($statistics.units -ne [string]$expectedUnits -or [int]$statistics.frames -le 0) {
                    throw "$scenario returned the wrong workload or recorded no rendered frames."
                }
                $windows = @{}
                foreach ($line in (Get-Content -LiteralPath $file | Where-Object { $_ -like 'RegimentPerfWindow *' })) {
                    $window = Read-Result $line
                    $windows.Add($window.name, $window)
                }
                if (!$Legacy) {
                    if ($statistics.state_hash -notmatch '^[0-9a-f]{16}$' -or
                        [int]$statistics.orbit_camera_ticks -lt 59 -or
                        !(Test-SampledWindow $windows.quick_orbit) -or [int]$windows.quick_orbit.steps -ne 60 -or
                        !$windows.ContainsKey('first_melee') -or !$windows.ContainsKey('artillery')) {
                        throw "The sample is missing required hash or diagnostic windows in $file."
                    }
                    if ($scenario -like 'Cannae*' -and
                        ([int]$statistics.first_melee_tick -le 0 -or !(Test-SampledWindow $windows.first_melee))) {
                        throw "The first melee hit and its timing window are missing in $file."
                    }
                    if ($scenario -eq 'Waterloo' -and
                        ([int]$statistics.first_artillery_tick -le 0 -or !(Test-SampledWindow $windows.artillery))) {
                        throw "The artillery events and their timing window are missing in $file."
                    }
                    if ($signatures.ContainsKey($scenario) -and $statistics.state_hash -ne $signatures[$scenario]) {
                        throw "The camera or presentation change altered the simulation state for $scenario."
                    }
                    $signatures[$scenario] = $statistics.state_hash
                }
                foreach ($windowName in @('overall', 'first_melee', 'artillery', 'quick_orbit')) {
                    if ($windowName -eq 'overall') {
                        $window = $statistics
                    } elseif (!$windows.ContainsKey($windowName)) {
                        continue
                    } else {
                        $window = $windows[$windowName]
                    }
                    [pscustomobject]@{
                        run = $iteration
                        scenario = $scenario
                        figures = $expectedUnits
                        camera = $camera
                        presentation = $presentation
                        window = $windowName
                        steps = $window.steps
                        frames = $window.frames
                        frame_p95_ms = $window.frame_p95_ms
                        frame_p99_ms = $window.frame_p99_ms
                        step_p95_ms = $window.step_p95_ms
                        step_p99_ms = $window.step_p99_ms
                        present_p95_ms = $window.present_p95_ms
                        present_p99_ms = $window.present_p99_ms
                        first_melee_tick = $statistics.first_melee_tick
                        first_artillery_tick = $statistics.first_artillery_tick
                        orbit_camera_ticks = $statistics.orbit_camera_ticks
                        state_hash = $statistics.state_hash
                    }
                }
            }
        }
    }
}

$rows | Export-Csv -LiteralPath (Join-Path $outputPath 'summary.csv') -NoTypeInformation -Encoding utf8
