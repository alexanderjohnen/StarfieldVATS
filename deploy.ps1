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
    $template = Join-Path $PSScriptRoot "res\StarfieldVATS.ini"
    if (-not (Test-Path $ini)) {
        Copy-Item $template $ini
    }
    else {
        # Seeding once means the deployed INI silently goes stale as new
        # settings are added: they fall back to their code defaults and
        # simply cannot be tuned, with nothing to indicate why. Found
        # 2026-08-25 with an INI two days behind and missing three whole
        # sections. Don't overwrite (that would discard tuned values) -
        # just say which keys are missing.
        $keyPattern = '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*='
        $have = @(Select-String -Path $ini -Pattern $keyPattern | ForEach-Object { $_.Matches[0].Groups[1].Value })
        $want = @(Select-String -Path $template -Pattern $keyPattern | ForEach-Object { $_.Matches[0].Groups[1].Value })
        $missing = @($want | Where-Object { $have -notcontains $_ })
        if ($missing.Count -gt 0) {
            Write-Warning "Deployed INI is missing $($missing.Count) setting(s) from res\StarfieldVATS.ini: $($missing -join ', ')"
            Write-Warning "They fall back to their code defaults. Copy the section(s) over from the template to tune them."
        }
    }

    Write-Host "Deployed to $pluginDir"
}
finally {
    Pop-Location
}
