# Ausgeführte Prüfung — 9. Oktober 2026

Causalis 0.2 wurde mit GCC/C++20 in Linux und nativ mit MSVC/x64 auf den GitHub-Runnern `windows-latest` und `windows-2022` gebaut und ausgeführt. Beide Windows-Jobs im [Run 37914071885](https://github.com/shedowe19/Causalis/actions/runs/37914071885) waren erfolgreich. Geprüfter Quellcommit: `3b9d6291d9b6ac8b13e7cc48092f1c68d512def6`. Die EXE-ZIPs stammen aus diesem Run; spätere Dokumentationskorrekturen ändern den geprüften Programmcode nicht.

## Ergebnisse

| Prüfung | Ergebnis |
| --- | --- |
| Portabler Kern, Dokumentadapter, Interpreter, Seitenpipeline, Checkpoints, CLI-Policy und JSON-CLI | Build mit -Wall -Wextra -Wpedantic -Werror erfolgreich |
| Renderertests | 22/22 bestanden |
| Dokumenttests | 23/23 bestanden, zusätzlich 1.500 deterministische fehlerhafte Eingaben |
| Eigene Skriptlaufzeit | 66 Testfälle bestanden, einschließlich 3.000 deterministischer fehlerhafter Quellen |
| Vault-Policy | 75 Prüfungen bestanden |
| Gemeinsame Seitenpipeline | Skripte standardmäßig aus, ausdrückliches lokales Opt-in, Remote-Ablehnung, tatsächliche Textmutation, Konsole, Mutations-/Arbeitsbudget-Rollback bestanden |
| Checkpoint-Integration | Passive Speicherung/erneute Importbereinigung, Textvergleich, Änderungen außerhalb der Vergleichspräfixe, Reihenfolge, fehlerhafte/trunkierte Dateien bestanden |
| Renderer-Robustheit | 2.000 deterministische fehlerhafte Dokumente und zwei lange Eingaben bestanden |
| CLI | Drei Beispiele in je drei Modi als JSON geprüft; Skriptlabor berechnet 15; ungültiges UTF-8 und ungültige Breite zurückgewiesen |
| AddressSanitizer / UndefinedBehaviorSanitizer | Gesamter portabler Prüflauf erfolgreich; keine gemeldeten Fehler |
| Debugvorschau | Aus der eigenen Display-Liste erzeugt und visuell geprüft; kein Windows-Screenshot |
| Windows / MSVC / PowerShell / CMake | Native x64-Release-Builds auf windows-latest und windows-2022 erfolgreich; PowerShell-Verpackung und Upload erfolgreich; alternatives windows/build.ps1 nicht ausgeführt |
| GitHub-Actions-Windowsprüfung | Beide Jobs erfolgreich; jeweils 7/7 CTest-Ziele einschließlich windows-native-smoke bestanden, dazu 9 CLI-Beispielmodi und 2 Prüfungen ungültiger Eingaben |
| Nativer Windows-Smoke-Test | Verborgenes Win32-Fenster, Controls, eigenes Rendering/GDI, lokale UTF-8-Datei und Skriptmutation, Suche, Leseansicht, Quellinspektion, Navigation, Tabs und Arbeitsbereiche auf beiden Runnern geprüft |
| Reale HTTPS-Dienste und echter Bitwarden-/Vaultwarden-Tresor | Nicht praktisch getestet |
| Web Platform Tests / Test262 / Sandbox / Autofill | Nicht ausgeführt beziehungsweise noch nicht implementiert |

LeakSanitizer konnte die benötigten Prozessinformationen in dieser Umgebung nicht lesen. Die ASan-/UBSan-Läufe verwendeten ASAN_OPTIONS=detect_leaks=0. Es besteht kein LeakSanitizer-Prüfnachweis. Interne begrenzte Tests sind kein vollständiger Sicherheits- oder Webstandardnachweis.

## Behobene konkrete Reviewbefunde

Der ursprüngliche Renderer behandelt Raw-Text-Endmarkierungen, inaktive Templates, lexikografische CSS-Prioritäten, begrenzte Entitysuche, geteilte Deklarationen, Selektor-Arbeitsbudgets, lange UTF-8-Wörter und Inline-Hintergründe. Die JSON-CLI gibt Zeichenketten korrekt aus und weist ungültiges UTF-8 zurück.

Neue Integrationsprüfungen führten zu Attributanzahl-/Allokationslimits, sicheren Grenzen für kombinierte Stile, Ausschluss versteckter/Formular-Titel aus passiven Seitenständen und echten Öffnungsmarkierungen vor Mutationen. Die Skriptbindung nutzt nur #id und dieselbe freigegebene Attribut-/Stilmenge wie der Dokumentadapter; ID-Änderungen sind ausgeschlossen. Mutationsanwendung hat ein eigenes Arbeitsbudget und verwirft Dokumentänderungen bei Überschreitung.

Der Vergleich meldet auch Textänderungen jenseits abgeschnittener Vergleichszeilen und reine Reihenfolgeänderungen. Darstellungsprefixe enden an UTF-8-Grenzen. Beim Projektimport bleibt serialisierte Herkunft unprivilegiert; die Datei kann keine lokale Skriptfreigabe übertragen.

Die Windows-Quellprüfung behandelte überholte Navigationsergebnisse, Quell-IDs/Titel geschlossener oder Hintergrund-Tabs, Tastaturbefehle und Timer-Reentranz beim Tresorergebnis. Die CLI-Bridge verwendet explizite lokale Pfade, gepinnte Dateisystemkomponenten, eine feste Befehlsliste, eingeschränkte Umgebung/Handles, Ausgabe-/Zeitgrenzen und verwirft rohe Ausgaben. Sie wurde nativ kompiliert; ihre tatsächlichen Prozess- und Tresorzugriffe bleiben praktisch zu prüfen. Der Status-vor-Serverwechsel ist gegenüber externen CLI-Prozessen nicht atomar.

## Wiederholen

```bash
python3 tools/test_portable.py --build-dir build-portable
python3 tools/test_portable.py --sanitize --build-dir build-sanitized
```

Nur bei nicht funktionierender LeakSanitizer-Unterstützung den zweiten Befehl um --disable-leak-check ergänzen. Auf Windows CMake/CTest und den Prüfablauf aus [windows/README.md](windows/README.md) durchführen oder den GitHub-Workflow erneut starten. Vor einer Freigabe als Alltagsbrowser fehlen insbesondere interaktive Bedien-/Accessibility-Prüfungen, reale HTTPS- und Tresortests, Webstandardkompatibilität und eine Prozesssandbox.
