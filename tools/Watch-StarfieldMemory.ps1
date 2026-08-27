<#
.SYNOPSIS
    Samples Starfield's memory, handle and thread usage over time into a CSV.

.DESCRIPTION
    Answers one question before any mod gets blamed for anything: is
    something actually growing without bound, or does the process reach a
    plateau and stay there?

    That distinction is the whole point. Starfield legitimately uses a lot
    of memory, and "a lot" is not a leak - a leak is memory that keeps
    climbing while you do nothing new. Leave this running for 20-30 minutes
    with the game idle (paused, or standing still in an interior) and the
    shape of the Private column tells you which one you have.

    Four numbers are recorded per sample, because "leak" covers more than
    one failure and they are not interchangeable:

      WorkingSetMB - RAM currently resident. Goes UP and DOWN on its own as
                     Windows trims it under pressure, so it is the WORST
                     column to judge a leak by, and the one Task Manager
                     shows by default.
      PrivateMB    - committed private memory. This is the leak indicator:
                     Windows cannot trim it, and it only falls when the
                     process actually frees something.
      Handles      - kernel handles. A handle leak (files, events, threads
                     never closed) degrades the whole system differently
                     from a memory leak and is invisible in the MB columns.
      Threads      - a thread leak costs ~1MB of stack each plus scheduler
                     pressure, and shows up here long before it shows up in
                     PrivateMB.

    Also records system-wide free RAM, since the mechanism that makes the
    WHOLE PC slow (rather than just the game) is the machine running out
    and paging everything else out to disk.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\Watch-StarfieldMemory.ps1

.EXAMPLE
    .\Watch-StarfieldMemory.ps1 -IntervalSeconds 30 -OutFile C:\temp\run2.csv
#>
[CmdletBinding()]
param(
    [int]    $IntervalSeconds = 15,
    [string] $ProcessName     = 'Starfield',
    [string] $OutFile
)

$ErrorActionPreference = 'Stop'

if (-not $OutFile) {
    $stamp   = Get-Date -Format 'yyyy-MM-dd_HHmmss'
    $OutFile = Join-Path $env:USERPROFILE "Desktop\starfield-memory_$stamp.csv"
}

Write-Host "Schreibe nach: $OutFile"
Write-Host "Warte auf Prozess '$ProcessName'... (Strg+C beendet die Messung)"
Write-Host ""

$samples   = @()
$firstPriv = $null
$seen      = $false

# Deliberately NOT Get-Counter: performance counter PATHS ARE LOCALISED
# ("\Memory\Available MBytes" does not exist on a German Windows, it is
# "\Arbeitsspeicher\Verfugbare MB"), so a counter-based script silently
# fails on exactly this machine. The CIM class properties below are
# language-independent.
function Get-SystemFreeMB {
    $os = Get-CimInstance Win32_OperatingSystem
    return [int]($os.FreePhysicalMemory / 1KB)
}

try {
    while ($true) {
        $proc = $null
        try { $proc = Get-Process -Name $ProcessName -ErrorAction Stop } catch { }

        if ($null -eq $proc) {
            if ($seen) {
                Write-Host "Prozess beendet - Messung fertig."
                break
            }
            Start-Sleep -Seconds $IntervalSeconds
            continue
        }

        # A process can legitimately appear more than once; sum so the
        # numbers stay meaningful instead of silently picking one.
        $ws   = ($proc | Measure-Object -Property WorkingSet64        -Sum).Sum
        $priv = ($proc | Measure-Object -Property PrivateMemorySize64 -Sum).Sum
        $hnd  = ($proc | Measure-Object -Property HandleCount         -Sum).Sum
        $thr  = ($proc | ForEach-Object { $_.Threads.Count } | Measure-Object -Sum).Sum

        $privMB = [math]::Round($priv / 1MB, 1)
        if ($null -eq $firstPriv) { $firstPriv = $privMB }
        $seen = $true

        $row = [pscustomobject]@{
            Time         = (Get-Date -Format 'HH:mm:ss')
            WorkingSetMB = [math]::Round($ws / 1MB, 1)
            PrivateMB    = $privMB
            GrowthMB     = [math]::Round($privMB - $firstPriv, 1)
            Handles      = $hnd
            Threads      = $thr
            SysFreeMB    = Get-SystemFreeMB
        }

        $samples += $row
        $row | Export-Csv -Path $OutFile -NoTypeInformation -Append -Encoding utf8

        $growth = $row.GrowthMB
        $sign = '+'
        if ($growth -lt 0) { $sign = '' }
        Write-Host ("{0}  Private {1,8} MB  ({2}{3} MB)   Handles {4,6}   Threads {5,4}   frei {6,6} MB" -f `
            $row.Time, $row.PrivateMB, $sign, $growth, $row.Handles, $row.Threads, $row.SysFreeMB)

        Start-Sleep -Seconds $IntervalSeconds
    }
}
finally {
    if ($samples.Count -ge 2) {
        $first = $samples[0]
        $last  = $samples[-1]
        $mins  = [math]::Max(1, $samples.Count * $IntervalSeconds / 60)

        Write-Host ""
        Write-Host "--- Zusammenfassung ($($samples.Count) Messpunkte) ---"
        Write-Host ("Private:  {0} MB -> {1} MB   ({2} MB, rund {3} MB/Stunde)" -f `
            $first.PrivateMB, $last.PrivateMB,
            [math]::Round($last.PrivateMB - $first.PrivateMB, 1),
            [math]::Round(($last.PrivateMB - $first.PrivateMB) / $mins * 60, 1))
        Write-Host ("Handles:  {0} -> {1}" -f $first.Handles, $last.Handles)
        Write-Host ("Threads:  {0} -> {1}" -f $first.Threads, $last.Threads)
        Write-Host ("Frei RAM: {0} MB -> {1} MB" -f $first.SysFreeMB, $last.SysFreeMB)
        Write-Host ""
        Write-Host "CSV: $OutFile"
    }
}
