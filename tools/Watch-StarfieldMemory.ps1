<#
.SYNOPSIS
    Samples memory, handle and thread usage of one or more processes into a CSV.

.DESCRIPTION
    Answers one question before any mod gets blamed for anything: is
    something actually growing without bound, or does the process reach a
    plateau and stay there?

    That distinction is the whole point. Starfield legitimately uses a lot
    of memory, and "a lot" is not a leak - a leak is memory that keeps
    climbing while you do nothing new. Leave this running for 20-30 minutes
    and the shape of the Private column tells you which one you have.

    Four numbers are recorded per process per sample, because "leak" covers
    more than one failure and they are not interchangeable:

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

    Several processes are watched from ONE window on purpose. Watching the
    game alone cannot tell "the game is leaking" apart from "something else
    on the machine is leaking and the game is merely present", and running
    a second copy of this script for the second process left a window
    behind every time (the run ends when its process exits, and OBS does
    not exit when the game does - found the annoying way, 2026-08-27).

    The run ends when the FIRST process in -ProcessName exits. The others
    are only along for comparison and do not keep the measurement alive.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\Watch-StarfieldMemory.ps1

.EXAMPLE
    .\Watch-StarfieldMemory.ps1 -ProcessName Starfield,obs64 -IntervalSeconds 30
#>
[CmdletBinding()]
param(
    [int]      $IntervalSeconds = 15,
    # Accepts either a real array or one comma-separated string, and is
    # split again below, because array arguments do not survive
    # "powershell -File": "-ProcessName Starfield,obs64" arrives there as
    # the single literal name "Starfield,obs64", which matches no process
    # at all - so the script sits there waiting, writing nothing, looking
    # exactly like a broken measurement rather than a bad argument
    # (2026-08-27, three restarts before this was spotted). Passing them as
    # two words is no better: the second binds to $IntervalSeconds.
    [string[]] $ProcessName     = @('Starfield', 'obs64'),
    [string]   $OutFile
)

$ErrorActionPreference = 'Stop'

if (-not $OutFile) {
    $stamp   = Get-Date -Format 'yyyy-MM-dd_HHmmss'
    $OutFile = Join-Path $env:USERPROFILE "Desktop\memory_$stamp.csv"
}

$names   = @($ProcessName -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$primary = $names[0]

Write-Host "Beobachte: $($names -join ", ")   (Lauf endet, wenn '$primary' beendet wird)"
Write-Host "CSV: $OutFile"
Write-Host "Warte auf '$primary'...  (Strg+C bricht ab)"
Write-Host ""

$first     = @{}   # Prozessname -> erster PrivateMB-Wert
$firstRow  = @{}
$lastRow   = @{}
$ticks     = 0
$seenPrimary = $false
$session   = 1

function Write-Summary {
    if ($script:ticks -lt 2) { return }
    $mins = [math]::Max(1, $script:ticks * $IntervalSeconds / 60)
    Write-Host ""
    Write-Host "--- Sitzung $script:session: $script:ticks Messpunkte, rund $([math]::Round($mins,1)) min ---"
    foreach ($n in $script:names) {
        if (-not $script:lastRow.ContainsKey($n)) { continue }
        $a = $script:firstRow[$n]
        $b = $script:lastRow[$n]
        $delta = [math]::Round($b.PrivateMB - $a.PrivateMB, 1)
        Write-Host ("{0,-12} Private {1} -> {2} MB   ({3} MB, rund {4} MB/Stunde)   Handles {5} -> {6}   Threads {7} -> {8}" -f `
            $n, $a.PrivateMB, $b.PrivateMB, $delta,
            [math]::Round($delta / $mins * 60, 1),
            $a.Handles, $b.Handles, $a.Threads, $b.Threads)
    }
}

# Deliberately NOT Get-Counter: performance counter PATHS ARE LOCALISED
# ("\Memory\Available MBytes" does not exist on a German Windows, it is
# "\Arbeitsspeicher\Verfugbare MB"), so a counter-based script silently
# fails on exactly this machine. The CIM class property below is
# language-independent.
function Get-SystemFreeMB {
    $os = Get-CimInstance Win32_OperatingSystem
    return [int]($os.FreePhysicalMemory / 1KB)
}

try {
    while ($true) {
        $freeMB  = Get-SystemFreeMB
        $stamp   = Get-Date -Format 'HH:mm:ss'
        $line    = "$stamp  "
        $anyRow  = $false

        foreach ($name in $names) {
            $proc = $null
            try { $proc = Get-Process -Name $name -ErrorAction Stop } catch { }

            if ($null -eq $proc) {
                # The primary going away ends the SESSION, not the run: the
                # summary is printed and the counters reset, then this keeps
                # waiting for the next launch. A/B testing a setting means
                # restarting the game between runs, and having to relaunch
                # the watcher each time was how three stray windows piled up
                # earlier today. Ctrl+C is what ends it for good.
                if ($name -eq $primary -and $seenPrimary) {
                    Write-Summary
                    $script:first = @{}; $script:firstRow = @{}; $script:lastRow = @{}
                    $script:ticks = 0
                    $script:seenPrimary = $false
                    $script:session++
                    Write-Host ""
                    Write-Host "Warte auf den naechsten Start von '$primary' (Sitzung $script:session)..."
                }
                continue
            }

            if ($name -eq $primary) { $seenPrimary = $true }

            # A process can legitimately appear more than once; sum so the
            # numbers stay meaningful instead of silently picking one.
            $ws   = ($proc | Measure-Object -Property WorkingSet64        -Sum).Sum
            $priv = ($proc | Measure-Object -Property PrivateMemorySize64 -Sum).Sum
            $hnd  = ($proc | Measure-Object -Property HandleCount         -Sum).Sum
            $thr  = ($proc | ForEach-Object { $_.Threads.Count } | Measure-Object -Sum).Sum

            $privMB = [math]::Round($priv / 1MB, 1)
            if (-not $first.ContainsKey($name)) { $first[$name] = $privMB }

            $row = [pscustomobject]@{
                Time         = $stamp
                Process      = $name
                WorkingSetMB = [math]::Round($ws / 1MB, 1)
                PrivateMB    = $privMB
                GrowthMB     = [math]::Round($privMB - $first[$name], 1)
                Handles      = $hnd
                Threads      = $thr
                SysFreeMB    = $freeMB
            }

            $row | Export-Csv -Path $OutFile -NoTypeInformation -Append -Encoding utf8
            if (-not $firstRow.ContainsKey($name)) { $firstRow[$name] = $row }
            $lastRow[$name] = $row
            $anyRow = $true

            $sign = '+'
            if ($row.GrowthMB -lt 0) { $sign = '' }
            $line += ("{0}: {1} MB ({2}{3})  H{4} T{5}   " -f `
                $name, $row.PrivateMB, $sign, $row.GrowthMB, $row.Handles, $row.Threads)
        }

        if ($anyRow) {
            $ticks++
            Write-Host ($line + "| frei $freeMB MB")
        }

        Start-Sleep -Seconds $IntervalSeconds
    }
}
catch [System.OperationCanceledException] { }
finally {
    if ($ticks -ge 2) {
        Write-Summary
        Write-Host ""
        Write-Host "CSV: $OutFile"
        # Only when a human is actually looking at a console window.
        # Unguarded this blocks forever in a background job or a redirected
        # shell, which is how the smoke test hung (2026-08-27).
        if ($Host.Name -eq 'ConsoleHost' -and [Environment]::UserInteractive) {
            Write-Host "Fenster bleibt offen - mit Enter schliessen."
            [void](Read-Host)
        }
    }
}
