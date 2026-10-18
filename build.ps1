param([switch]$Clean)
$ErrorActionPreference = 'Stop'
$kitRoot = if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' }
$bashPath = Join-Path $kitRoot 'msys2\usr\bin\bash.exe'
if (-not (Test-Path -LiteralPath $bashPath)) {
    throw 'Install devkitPro with devkitARM and libgba, then set DEVKITPRO.'
}
$env:DEVKITPRO = $kitRoot.Replace('\', '/')
$env:DEVKITARM = ($kitRoot.TrimEnd('\', '/') + '/devkitARM').Replace('\', '/')
Push-Location $PSScriptRoot
try {
    if (-not $Clean) {
        if (-not $env:GBA_PYTHON) {
            $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
            if (-not $pythonCommand) { throw 'Python 3.10+ and Pillow are required. Set GBA_PYTHON to python.exe.' }
            $env:GBA_PYTHON = $pythonCommand.Source
        }
    }
    if ($Clean) {
        & $bashPath --noprofile --norc ./scripts/build.sh clean
    } else {
        & $bashPath --noprofile --norc ./scripts/build.sh
    }
    if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }
