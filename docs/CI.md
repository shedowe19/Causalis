# Windows-Prüfung mit GitHub Actions

Der Workflow [.github/workflows/windows.yml](../.github/workflows/windows.yml) baut das Projekt nativ auf `windows-latest` und zusätzlich auf `windows-2022`. Es ist kein Cross-Compile. Beide Jobs verwenden MSVC, Windows SDK, den echten Win32-Host und die WinHTTP-/CLI-Quelldateien.

## Im Repository starten

Die Projektdateien müssen im Repository-Wurzelverzeichnis liegen: `CMakeLists.txt`, `src/`, `windows/`, `tests/`, `samples/`, `tools/` und `.github/`. Beim Entpacken des Pakets den Inhalt des Projektordners verwenden, nicht den ganzen Projektordner als zusätzliche Ebene hochladen.

Der Workflow startet bei Push und Pull Request. Für den manuellen Start muss der Workflow auf dem Standardbranch vorhanden sein: **Actions → Windows build and test → Run workflow**. Die GitHub-App braucht Zugriff auf das Zielrepository und zum Hochladen von Workflows die entsprechende Berechtigung.

## Was geprüft wird

1. CMake konfiguriert den nativen x64-MSVC-Build und baut Browser, CLI und Prüfprogramme.
2. CTest führt die portablen Modul-/Integrationstests und den Win32-Test `causalis.exe --smoke-test` aus.
3. Der native Test erzeugt ein verborgenes echtes Fenster und Steuerelemente, verarbeitet ein lokales Testdokument und die eigene Skriptmutation und prüft GDI-Ausgabe, Navigation, Tabs und Arbeitsbereiche. Er öffnet keine interaktiven Dialoge, lädt keine Website und greift nicht auf einen echten Tresor zu.
4. Die gebaute Windows-CLI verarbeitet drei Beispiele in je drei Modi und weist ungültiges UTF-8 und ungültige Breiten zurück.
5. Erst wenn diese Schritte erfolgreich sind, wird das Anwendungsartefakt mit beiden EXEs, Beispielen, Dokumentation, Quellcommit und SHA-256-Prüfsummen erstellt.

## Downloads und Fehler

Der erfolgreiche [Run 37914071885](https://github.com/shedowe19/Causalis/actions/runs/37914071885) bietet die ZIPs [Causalis-windows-latest-x64](https://github.com/shedowe19/Causalis/actions/runs/37914071885/artifacts/11608871200) und [Causalis-windows-2022-x64](https://github.com/shedowe19/Causalis/actions/runs/37914071885/artifacts/11609310132) unter **Artifacts**. Nach dem Entpacken `causalis.exe` starten. Es ist eine unsignierte Entwicklungsanwendung, kein Installer und kein Nachweis vollständiger Webkompatibilität. Diese Artefakte laufen am 23. Oktober 2026 ab; ein neuer erfolgreicher Run erzeugt neue Downloads.

Alle MSVC-Ziele verwenden einheitlich die statische C/C++-Laufzeit (/MT in Release), damit diese Laufzeit nicht als zusätzliche DLL-Installation vorausgesetzt wird. Ein Runner-Ergebnis ersetzt trotzdem keine Prüfung auf einer sauberen Windows-Installation.

Testberichte werden auch bei Fehlern separat hochgeladen: CTest-JUnit, CLI-Bericht, Win32-Bericht und CTest-Protokoll, soweit der fehlgeschlagene Lauf sie erzeugt hat. Buildfehler stehen im Jobprotokoll. Die beiden Matrixjobs brechen sich nicht gegenseitig ab; ein fehlgeschlagener Job erhält kein Anwendungsartefakt.

## Verifikation und Grenzen

Am 9. Oktober 2026 wurden beide Jobs für Quellcommit `3b9d6291d9b6ac8b13e7cc48092f1c68d512def6` erfolgreich abgeschlossen. Je Runner bestanden 7/7 CTest-Ziele einschließlich des nativen Win32-Tests und zusätzlich 9 CLI-Beispielmodi sowie 2 Prüfungen ungültiger Eingaben. Beide Anwendungs-ZIPs und beide Testbericht-Artefakte wurden hochgeladen. `BUILD.txt` in jedem Anwendungs-ZIP hält den getesteten Quellcommit fest.

Die Prüfung ersetzt keine interaktive Accessibility-/Bedienprüfung, echte HTTPS-Tests, Bitwarden-/Vaultwarden-Testkonten oder einen Sandboxnachweis. Der abgeschlossene Run belegt den nativen Build und die grundlegende Windows-Ausführung für den angegebenen Quellcommit. Nachfolgende reine Dokumentationskorrekturen wurden mit `[skip ci]` veröffentlicht und verändern keine Programm- oder Workflowdateien.

Die Actions-Versionen sind auf verifizierte Release-Commits festgelegt. Der Workflow benötigt nur lesenden Repositoryzugriff und keine Tresorgeheimnisse. Artefakte werden 14 Tage aufbewahrt.

Primärquellen, geprüft am 9. Oktober 2026:

- [GitHub: Auswahl des Runners](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job)
- [Checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1)
- [Upload Artifact v7.0.2](https://github.com/actions/upload-artifact/releases/tag/v7.0.2)
