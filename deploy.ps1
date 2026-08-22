# Builds the plugin and copies it into the Starfield SFSE plugin folder.
$ErrorActionPreference = "Stop"

$gameDir = "G:\Program Files (x86)\Steam\steamapps\common\Starfield"
$pluginDir = Join-Path $gameDir "Data\SFSE\Plugins"
$buildOut = Join-Path $PSScriptRoot "build\windows\x64\releasedbg"

Push-Location $PSScriptRoot
try {
    xmake build -y
    if ($LASTEXITCODE -ne 0) { throw "build failed" }

    Copy-Item (Join-Path $buildOut "StarfieldVATS.dll") $pluginDir -Force
    $pdb = Join-Path $buildOut "StarfieldVATS.pdb"
    if (Test-Path $pdb) { Copy-Item $pdb $pluginDir -Force }

    # Never overwrite the user's tuned INI; only seed it on first deploy.
    $ini = Join-Path $pluginDir "StarfieldVATS.ini"
    if (-not (Test-Path $ini)) {
        Copy-Item (Join-Path $PSScriptRoot "res\StarfieldVATS.ini") $ini
    }

    Write-Host "Deployed to $pluginDir"
}
finally {
    Pop-Location
}
