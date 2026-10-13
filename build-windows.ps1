param([string]$JucePath = 'C:\JUCE', [string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakeExe = $cmakeCommand.Source } else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakeExe = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (!(Test-Path -LiteralPath $cmakeExe)) { throw 'Install Visual Studio 2022 Desktop development with C++ and CMake tools.' }
& $cmakeExe -S $PSScriptRoot -B "$PSScriptRoot\build" -G 'Visual Studio 17 2022' -A x64 "-DJUCE_DIR=$JucePath"
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
& $cmakeExe --build "$PSScriptRoot\build" --config $Configuration --target MachinedrumModule_VST3 MachinedrumModule_Standalone md-smoke --parallel 6
if ($LASTEXITCODE) { throw 'Build failed.' }
Write-Host "Standalone: $PSScriptRoot\dist\Standalone\$Configuration"
Write-Host "VST3: $PSScriptRoot\build\MachinedrumModule_artefacts\$Configuration\VST3"
