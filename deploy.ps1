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

    $ini = Join-Path $pluginDir "StarfieldVATS.ini"
    $template = Join-Path $PSScriptRoot "res\StarfieldVATS.ini"
    # An INI key is only read from the section it was asked for, so a key
    # in the WRONG section is invisible to the plugin while looking
    # perfectly correct in the file. That is not hypothetical: bDebugAimMarkers
    # was added under [Resource] instead of [HUD] on 2026-08-26 and the
    # diagnostic it gates silently stayed off for two whole test sessions.
    # So every check below compares section AND key, never the key alone.
    function Get-IniKeys([string]$path) {
        $map = @{}
        $section = ""
        foreach ($line in [System.IO.File]::ReadAllLines($path)) {
            if ($line -match '^\s*\[([^\]]+)\]') { $section = $Matches[1]; continue }
            if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=') { $map[$Matches[1]] = $section }
        }
        return $map
    }

    # Settings.cpp is the source of truth for which settings exist. The
    # deployed-vs-template check below could not catch a setting that was
    # never added to the template in the first place - which is exactly how
    # eleven of them silently became untunable (found 2026-08-25, by
    # Alexander asking whether the INI had been kept in sync). So check the
    # template against the code first.
    $settingsCpp = Join-Path $PSScriptRoot "src\Settings.cpp"
    if ((Test-Path $settingsCpp) -and (Test-Path $template)) {
        $codeKeys = @{}
        foreach ($m in [regex]::Matches(
            [System.IO.File]::ReadAllText($settingsCpp),
            'GetPrivateProfile(?:IntA|FloatA|StringA)\(\s*"([^"]+)"\s*,\s*"([^"]+)"')) {
            $codeKeys[$m.Groups[2].Value] = $m.Groups[1].Value
        }
        $templateKeys = Get-IniKeys $template

        $undocumented = @($codeKeys.Keys | Where-Object { -not $templateKeys.ContainsKey($_) } | Sort-Object)
        if ($undocumented.Count -gt 0) {
            Write-Warning "res\StarfieldVATS.ini is missing $($undocumented.Count) setting(s) that Settings.cpp reads: $($undocumented -join ', ')"
            Write-Warning "Add them to the template, or they can never be tuned by anyone."
        }
        $wrongSection = @($codeKeys.Keys | Where-Object {
            $templateKeys.ContainsKey($_) -and $templateKeys[$_] -ne $codeKeys[$_]
        } | ForEach-Object { "$_ (template [$($templateKeys[$_])], code reads [$($codeKeys[$_])])" } | Sort-Object)
        if ($wrongSection.Count -gt 0) {
            Write-Warning "res\StarfieldVATS.ini has $($wrongSection.Count) setting(s) under the wrong section: $($wrongSection -join '; ')"
            Write-Warning "A key outside its section is never read - it silently falls back to the code default."
        }
    }

    # Never overwrite the user's tuned INI; only seed it on first deploy.
    if (-not (Test-Path $ini)) {
        Copy-Item $template $ini
    }
    else {
        # Seeding once means the deployed INI silently goes stale as new
        # settings are added: they fall back to their code defaults and
        # simply cannot be tuned, with nothing to indicate why. Found
        # 2026-08-25 with an INI two days behind and missing three whole
        # sections. Don't overwrite (that would discard tuned values) -
        # just say which keys are missing or misplaced.
        $have = Get-IniKeys $ini
        $want = Get-IniKeys $template

        $missing = @($want.Keys | Where-Object { -not $have.ContainsKey($_) } | Sort-Object)
        if ($missing.Count -gt 0) {
            Write-Warning "Deployed INI is missing $($missing.Count) setting(s) from res\StarfieldVATS.ini: $($missing -join ', ')"
            Write-Warning "They fall back to their code defaults. Copy the section(s) over from the template to tune them."
        }
        $misplaced = @($want.Keys | Where-Object {
            $have.ContainsKey($_) -and $have[$_] -ne $want[$_]
        } | ForEach-Object { "$_ (is under [$($have[$_])], belongs under [$($want[$_])])" } | Sort-Object)
        if ($misplaced.Count -gt 0) {
            Write-Warning "Deployed INI has $($misplaced.Count) setting(s) under the wrong section: $($misplaced -join '; ')"
            Write-Warning "A key outside its section is never read - it silently falls back to the code default."
        }
    }

    Write-Host "Deployed to $pluginDir"
}
finally {
    Pop-Location
}
