$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1')
$emulatorPath = $env:GBA_EMULATOR_GUI
if (-not $emulatorPath) {
    $command = Get-Command mGBA.exe -ErrorAction SilentlyContinue
    if ($command) { $emulatorPath = $command.Source }
}
if (-not $emulatorPath -or -not (Test-Path -LiteralPath $emulatorPath)) {
    throw 'ROM built. Set GBA_EMULATOR_GUI to mGBA.exe or add mGBA.exe to PATH.'
}
$romPath = Join-Path $PSScriptRoot 'dist/galgame-framework.gba'
Start-Process -FilePath $emulatorPath -ArgumentList ('"' + $romPath + '"') -WorkingDirectory $PSScriptRoot -WindowStyle Hidden
