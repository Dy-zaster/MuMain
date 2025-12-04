[CmdletBinding()]
param(
    [ValidateSet('GlobalDebug', 'GlobalRelease')]
    [string]$Configuration = 'GlobalDebug',
    [ValidateSet('Win32', 'x86', 'x64')]
    [string]$Platform = 'x86',
    [switch]$SkipManaged,
    [switch]$SkipNative,
    [switch]$Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-MSBuildPath {
    $msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($msbuild) {
        return $msbuild.Source
    }

    $vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vsWhere) {
        $installationPath = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    throw 'MSBuild was not found. Make sure Visual Studio Build Tools are installed or add msbuild.exe to PATH.'
}

$repoRoot = Split-Path -Parent $PSCommandPath
$sourceRoot = Join-Path $repoRoot 'Source Main 5.2'
$nativeConfig = if ($Configuration -ieq 'GlobalRelease') { 'Global Release' } else { 'Global Debug' }
$managedConfig = if ($Configuration -ieq 'GlobalRelease') { 'Release' } else { 'Debug' }
$publishOutput = Join-Path $sourceRoot $nativeConfig

if (-not (Test-Path $sourceRoot)) {
    throw "Could not find the native source folder at '$sourceRoot'. Run this script from the repository root."
}

Push-Location $repoRoot
try {
    if (-not $SkipManaged) {
        Write-Host "Publishing MUnique.Client.Library ($managedConfig) to '$publishOutput'..." -ForegroundColor Cyan
        New-Item -ItemType Directory -Path $publishOutput -Force | Out-Null
        $dotnetArgs = @(
            'publish', 'ClientLibrary/MUnique.Client.Library.csproj',
            '-c', $managedConfig,
            '-r', 'win-x86',
            '--self-contained', 'true',
            '-p:PublishAot=true',
            '-p:Platform=x86',
            '-p:AppendTargetFrameworkToOutputPath=false',
            '-p:AppendRuntimeIdentifierToOutputPath=false',
            '-o', $publishOutput
        )
        if ($Rebuild) {
            $dotnetArgs += '--force'
        }

        dotnet @dotnetArgs
        if ($LASTEXITCODE -ne 0) {
            throw "dotnet publish failed with exit code $LASTEXITCODE."
        }
    } else {
        Write-Host 'Skipping managed publish step.' -ForegroundColor Yellow
    }

    if (-not $SkipNative) {
        $solutionPlatform = if ($Platform -ieq 'Win32') { 'x86' } else { $Platform }
        $msbuildPath = Get-MSBuildPath
        $solutionPath = Join-Path $sourceRoot 'Main.sln'
        Write-Host "Building native client ($nativeConfig|$solutionPlatform) using '$msbuildPath'..." -ForegroundColor Cyan
        $msbuildTarget = if ($Rebuild) { '/t:Rebuild' } else { '/t:Build' }
        $msbuildArgs = @(
            $solutionPath,
            "/p:Configuration=`"$nativeConfig`"",
            "/p:Platform=$solutionPlatform",
            $msbuildTarget
        )

        & $msbuildPath @msbuildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "MSBuild failed with exit code $LASTEXITCODE."
        }
    } else {
        Write-Host 'Skipping native build step.' -ForegroundColor Yellow
    }
}
finally {
    Pop-Location
}
