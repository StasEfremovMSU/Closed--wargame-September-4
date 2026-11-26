[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameArchive,
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string]$EngineRoot,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$Version,
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9._-]+$')][string]$OutputName,
    [string]$OutputDirectory,
    [string]$WorkDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = Split-Path -Parent $PSScriptRoot
$zip = (Resolve-Path -LiteralPath $GameArchive).Path
$iscc = (Resolve-Path -LiteralPath $Compiler).Path
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$runtime = Join-Path $engine 'Engine/Extras/Redist/en-us/vc_redist.x64.exe'
$versionInfo = (Get-Item -LiteralPath $runtime).VersionInfo
$signature = Get-AuthenticodeSignature -LiteralPath $runtime
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '^CN=Microsoft Corporation,') {
    throw 'The bundled Microsoft VC Runtime must have a valid Microsoft Authenticode signature.'
}
if ($versionInfo.FileVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw "Unexpected runtime version: $($versionInfo.FileVersion)"
}

if (!$OutputDirectory) { $OutputDirectory = Join-Path $project 'Distribution' }
if (!$WorkDirectory) { $WorkDirectory = Join-Path $project 'Saved/InstallerBuild' }
$output = (New-Item -ItemType Directory -Force -Path $OutputDirectory).FullName
$work = (New-Item -ItemType Directory -Force -Path $WorkDirectory).FullName
$temp = (New-Item -ItemType Directory -Path (Join-Path $work ([guid]::NewGuid().ToString('N')))).FullName

try {
    $checksumPath = [IO.Path]::ChangeExtension($zip, '.sha256')
    if (Test-Path -LiteralPath $checksumPath -PathType Leaf) {
        $expectedHash = ((Get-Content -LiteralPath $checksumPath -Raw).Trim() -split '\s+')[0]
        if ($expectedHash -notmatch '^[a-fA-F0-9]{64}$' -or
            (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash -ine $expectedHash) {
            throw 'The game archive does not match its published SHA-256.'
        }
    }

    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $temp)
    $game = Join-Path $temp 'AgeOfBattles-Windows-x64'
    foreach ($required in @(
        'AgeOfBattlesStas.exe',
        'AgeOfBattlesStas/Binaries/Win64/AgeOfBattlesStas-Win64-Shipping.exe',
        'AgeOfBattlesStas/Content/Paks/AgeOfBattlesStas-Windows.utoc',
        'AgeOfBattlesStas/Content/Paks/AgeOfBattlesStas-Windows.ucas'
    )) {
        if (!(Test-Path -LiteralPath (Join-Path $game $required) -PathType Leaf)) {
            throw "The game archive is missing $required."
        }
    }
    if (Get-ChildItem -LiteralPath $game -Recurse -File -Filter '*.pdb') {
        throw 'Use a Shipping distribution archive without debug symbols.'
    }

    $source = Join-Path $project 'Build/Windows/AgeOfBattles.iss'
    $options = @("/DGamePayload=$game", "/DVcRuntime=$runtime", "/DVcVersion=$($versionInfo.FileVersion)",
        "/DReleaseVersion=$Version", "/DSetupOutput=$temp", "/DSetupName=$OutputName", $source)
    & $iscc @options
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE." }

    $compiled = Join-Path $temp ($OutputName + '.exe')
    $asset = Join-Path $output ($OutputName + '.exe')
    $digest = (Get-FileHash -Algorithm SHA256 -LiteralPath $compiled).Hash.ToLowerInvariant()
    $size = (Get-Item -LiteralPath $compiled).Length
    Move-Item -LiteralPath $compiled -Destination $asset -Force
    [IO.File]::WriteAllText([IO.Path]::ChangeExtension($asset, '.sha256'),
        "$digest  $OutputName.exe" + [Environment]::NewLine, [Text.Encoding]::ASCII)
    [pscustomobject]@{ Path = $asset; Bytes = $size; Sha256 = $digest; VcRuntimeVersion = $versionInfo.FileVersion } |
        ConvertTo-Json -Compress
} finally {
    $safeWork = (Resolve-Path -LiteralPath $work).Path
    $safeTemp = (Resolve-Path -LiteralPath $temp).Path
    if ((Split-Path -Parent $safeTemp) -ieq $safeWork -and (Split-Path -Leaf $safeTemp) -match '^[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $safeTemp -Recurse -Force
    }
}
