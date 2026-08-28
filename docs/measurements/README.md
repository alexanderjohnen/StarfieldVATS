# Rohdaten der Speicherleck-Suche, 27./28.08.2026

Erhoben mit `tools/Watch-StarfieldMemory.ps1`. Jede Datei ist eine
Sitzung. **Die aussagekräftige Spalte ist `PrivateMB`**, nicht
`WorkingSetMB` — den kürzt Windows eigenständig, und genau das hat das
Leck verborgen (12 GB im Task-Manager, während der Prozess 29 GB
zugesichert hatte).

Alle Läufe fanden in derselben Szene und bei derselben Tätigkeit statt
(in der Starmap sitzen, OBS läuft), damit der Vergleich trägt.

| Datei | Bedingung | Ergebnis |
|---|---|---|
| `01_ohne-mod_kontrolllauf.csv` | DLL umbenannt, 6,2 min | −0,9 MB — flach |
| `02_ohne-mod_obs-parallel.csv` | OBS im selben Zeitraum | −81,5 MB — entlastet OBS |
| `03_mit-mod_das-leck.csv` | Mod aktiv, 4 min | **+8.545 MB** (~2 GB/min, streng linear) |
| `04_stufenleiter_0-1-2.csv` | `iOverlayStage` 0, 1, 2 nacheinander | alle flach → grenzt auf eine Zeile ein |
| `05_nach-dem-fix.csv` | Fix aktiv, Starmap **und** Dungeon-Kampf | −9,9 MB bzw. −265 MB |

Beim Ablesen zwei Fallstricke, über die ich selbst gestolpert bin:

- **Die Ladephase gehört nicht dazu.** Beim Laden wachsen 3 GB auf 16 GB,
  das ist legitim. Aussagekräftig ist erst das Plateau danach. Die
  `GrowthMB`-Spalte rechnet ab dem ersten Messpunkt und ist deshalb in
  Läufen, die vor dem Laden beginnen, irreführend.
- **Datei 04 enthält mehrere Sitzungen.** Ein starker Abfall in
  `PrivateMB` markiert einen Neustart des Spiels, also die nächste Stufe.

Die Auswertung und die Schlussfolgerungen stehen in `../../HANDOFF.md`.
