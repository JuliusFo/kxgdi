# kxgdi – gdi32-Erweiterung für VxKex (Windows 7, x64)

[English version](README.md)

`kxgdi.dll` ergänzt [VxKex](https://github.com/i486/VxKex) um Funktionen aus `gdi32.dll`, die es erst ab Windows 8 gibt.
Alle anderen gdi32-Funktionen (994 Stück) werden unverändert an die echte `gdi32.dll` weitergereicht.

Bisher nachgerüstet:

| Funktion | Ab Windows | Nachbau unter Windows 7 |
|---|---|---|
| `D3DKMTOpenAdapterFromLuid` | 8 | Alle Grafikadapter durchgehen (SetupAPI-Geräteschnittstellen, notfalls GDI-Anzeigenamen), jeden mit `D3DKMTOpenAdapterFromDeviceName` bzw. `D3DKMTOpenAdapterFromGdiDisplayName` öffnen, LUID vergleichen, passendes Handle zurückgeben. |

Anlass: Path of Exile startet unter Windows 7 nicht mehr („Der Prozedureinsprungpunkt
D3DKMTOpenAdapterFromLuid wurde in der DLL GDI32.dll nicht gefunden“).

## Stand

- ✅ Unter Windows 11 gebaut und getestet (`kxgdi-test.exe`): Beide Suchwege finden den Adapter,
  Rückgabewerte stimmen mit der echten Windows-Funktion überein (Erfolg bzw. `0xC000000D` bei unbekannter LUID),
  weitergeleitete Funktionen landen in `gdi32.dll`.
- ✅ Die DLL importiert nur Funktionen, die es unter Windows 7 gibt, und ist als Windows 6.01 markiert.
- ⚠️ **Unter Windows 7 noch nicht getestet.** Der erste Schritt ist deshalb `kxgdi-test.exe` auf dem Windows-7-PC.
- Nur 64 Bit (Path of Exile ist 64 Bit).

## Wichtig vorab

- **Grafikproblem bleibt:** Selbst wenn PoE damit startet, lehnt DirectX 11 unter Windows 7 die neuen
  Partikel-Shader weiterhin ab, und der Vulkan-Treiber 471.96 stürzt ab. Für Effekte braucht es zusätzlich DXVK.
- **Nutzungsbedingungen:** VxKex und kxgdi klinken sich in den Spielprozess ein. Ein Bann ist unwahrscheinlich,
  aber nicht ausgeschlossen. Im Zweifel vorher beim GGG-Support nachfragen.
- Es kann weitere fehlende Funktionen geben. Windows meldet immer nur die erste.

## Anleitung für den Windows-7-PC

**Voraussetzungen:** Windows 7 SP1 64 Bit, **VxKex ab Version 1.2.0** (empfohlen: 1.2.2.2316 von
`github.com/i486/VxKex/releases`). Ältere Versionen und *VxKex NEXT* kennen die nötige Einstellung
`KEX_DllRewriteEntries` nicht.

1. **Paket entpacken**, z. B. nach `C:\kxgdi`.

2. **Selbsttest:** Eingabeaufforderung öffnen und ausführen:
   ```
   cd /d C:\kxgdi
   kxgdi-test.exe
   ```
   Erwartet: viele `[ OK ]`, am Ende `RESULT: all checks passed`. Die Zeile
   `No native D3DKMTOpenAdapterFromLuid (Windows 7)` ist dabei richtig.
   Bei `[FAIL]`: Ausgabe in einem Issue posten und **nicht weitermachen**.

3. **Pfad der `.tmp`-Datei herausfinden.** Die Fehlermeldung nannte `PathOfExile.tmp`. Während das Fehlerfenster
   offen ist: Task-Manager → Reiter „Prozesse“ → Rechtsklick auf `PathOfExile.tmp` → „Dateipfad öffnen“.

4. **Installieren:** PowerShell **als Administrator** öffnen (64 Bit, nicht „x86“) und ausführen
   (Pfade anpassen, beide Programme angeben):
   ```
   cd C:\kxgdi
   powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -ExePath "C:\Program Files (x86)\Steam\steamapps\common\Path of Exile\PathOfExileSteam.exe","<Pfad zur PathOfExile.tmp>"
   ```
   Das Skript
   - kopiert `kxgdi.dll` nach `<VxKex>\Kex64\`,
   - aktiviert VxKex für die angegebenen Programme,
   - setzt in deren VxKex-Einstellungen `KEX_DllRewriteEntries = gdi32:kxgdi|api-ms-win-dx-d3dkmt:kxgdi`.
     Vorhandene andere Einträge bleiben erhalten.

5. **Mitlesen (optional, aber hilfreich):** Sysinternals **DebugView** starten. kxgdi meldet jeden Aufruf mit
   `KxGdi: D3DKMTOpenAdapterFromLuid(...) -> ...`.

6. **PoE starten.** Mögliche Ergebnisse:
   - PoE startet → nächster Schritt ist DXVK für die Effekte.
   - Neue Meldung „Einsprungpunkt … nicht gefunden“ → Screenshot in einem Issue posten. Das ist die nächste
     fehlende Funktion, die nachgerüstet werden muss.
   - Absturz → Ereignisanzeige (Anwendung, „Application Error“) und DebugView-Ausgabe in einem Issue posten.

**Rückgängig machen:**
```
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -ExePath "<gleiche Pfade wie oben>" -Uninstall
```
Entfernt die Umleitung und `kxgdi.dll`. VxKex selbst bleibt aktiviert und lässt sich im Eigenschaften-Dialog
(Reiter „VxKex“) abschalten.

## Technische Details

- VxKex schreibt beim Laden die DLL-Namen in den Importtabellen der Programm-DLLs um. Mit
  `KEX_DllRewriteEntries` (IFEO, ab VxKex 1.2.0) lässt sich das pro Programm erweitern, ohne VxKex neu zu bauen.
  Der neue Name darf nicht länger als der alte sein: `kxgdi` und `gdi32` haben beide 5 Zeichen.
- `kxgdi.dll` muss in `<VxKex>\Kex64` liegen. DLLs dort schreibt VxKex nicht um, deshalb erreicht der eigene
  gdi32-Import von kxgdi die echte `gdi32.dll`. Liegt die DLL woanders, würde VxKex ihren gdi32-Import auf sie
  selbst umbiegen.
- Die Weiterleitungen stammen aus der `gdi32.dll` von Windows 11 (995 Namen). Weiterleitungen auf Namen, die es
  unter Windows 7 nicht gibt, schaden nicht: Sie schlagen nur fehl, wenn ein Programm genau diese Funktion
  importiert, und das passiert ohne kxgdi genauso. Programme, die mit einem aktuellen SDK gebaut sind (wie PoE),
  können ohnehin nur Namen importieren, die es in der heutigen gdi32 gibt.

## Bauen

Voraussetzung: Visual Studio bzw. Build Tools mit den MSVC-x64-Werkzeugen. Ein Windows-SDK ist **nicht** nötig,
denn der Code nutzt keine SDK-Header und keine C-Laufzeitbibliothek, und die Import-Bibliotheken werden aus
`imports\*.def` erzeugt.

```
pwsh .\build.ps1 -Test              # baut out\x64\kxgdi.dll + kxgdi-test.exe und testet
pwsh .\build.ps1 -RegenerateDef     # Weiterleitungsliste aus der lokalen gdi32.dll neu erzeugen
pwsh .\tools\gen-def.ps1 -Gdi32 C:\Windows\System32\gdi32.dll, D:\win7\gdi32.dll   # Win7-Exporte ergänzen
pwsh .\tools\package.ps1            # dist\kxgdi-win7-x64.zip für den Windows-7-PC
```

Release-Dateien baut GitHub Actions aus dem getaggten Quellcode (`.github\workflows\build.yml`). Auf
Build-Servern ohne Grafikadapter läuft der Test mit `/ci` und überspringt die Adapter-Prüfungen.

Weitere Funktionen nachrüsten: In `src\kxgdi.c` als `Ext_<Name>` implementieren, in `tools\gen-def.ps1` in
`$Implemented` eintragen, `build.ps1 -RegenerateDef -Test` ausführen.

| Datei | Inhalt |
|---|---|
| `src\kxgdi.c` | Implementierung |
| `src\kxgdi.def` | Exporte (generiert) |
| `imports\*.def` | Import-Bibliotheken ohne SDK (`gdi32.def` generiert) |
| `test\test.c` | Selbsttest, läuft unter Windows 7 und neuer |
| `install\Install-KxGdi.ps1` | Installation auf dem Windows-7-PC (PowerShell 2.0) |
| `tools\gen-def.ps1`, `tools\package.ps1` | Generator und Paketbau |
| `.github\workflows\build.yml` | CI-Build und Releases |

## Lizenz

[MIT](LICENSE). Kein Bezug zu Microsoft, Grinding Gear Games oder den VxKex-Autoren.
