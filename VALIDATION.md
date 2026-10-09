# Prüfung von Causalis 0.3

Am 9. Oktober 2026 wurde die Browserbasis auf Chromium/Electron umgestellt. Ergebnisse der früheren eigenen Engine gelten nicht als Nachweis für diesen Stand.

Der native [Windows-Run 37918949653](https://github.com/shedowe19/Causalis/actions/runs/37918949653) ist erfolgreich abgeschlossen. Getesteter Quellcommit: `8d2e519b405d095362a59fedd6d857284b35da4c`. Nachfolgende Dokumentationsänderungen verändern diese geprüfte Anwendung nicht.

| Prüfung | windows-latest | windows-2022 |
| --- | --- | --- |
| JavaScript-Syntax | 15 Dateien bestanden | 15 Dateien bestanden |
| Node-Regressionstests | 32/32 bestanden | 32/32 bestanden |
| Offizieller Bitwarden-OSS-Client | Download/SHA-256/Version 2026.9.1 bestanden | Download/SHA-256/Version 2026.9.1 bestanden |
| Tatsächliches Entwicklungs-Electron | 16/16 bestanden | 16/16 bestanden |
| Fertige Causalis.exe | 16/16 bestanden | 16/16 bestanden |
| Anwendung und Testberichte hochgeladen | Ja | Ja |

Die Node-Regressionstests prüfen tatsächliche Eingabegrenzen, CLI-Umgebungen und stdin, Server-/URI-Zuordnung, zeitlich begrenzte Auswahl, Sperren während laufender Abrufe, Workspace-Wechsel und Formularprüfungen. Die eigene UI sowie der Main-Prozess wurden zusätzlich unabhängig auf IPC- und Geheimnisgrenzen geprüft.

Der Smoke-Test startet sowohl Electron aus dem Entwicklungsstand als auch die fertig gepackte EXE. Er verwendet Chromium selbst für CSS Grid, Flexbox, SVG, PNG, Canvas, JavaScript, Formulare, Navigation und Screenshots. Er prüft UI-/Website-API-Isolation, fremde und veraltete Autofill-Ziele sowie das Fehlen einer Chrome-Passwortdatenbank im frischen Testprofil. Sandbox, Zertifikatsprüfung und Websicherheit bleiben aktiviert.

Die offizielle CLI prüft mit einem isolierten temporären Profil den unauthentifizierten Status. Anmeldung, Eintragsabruf und Autofill verwenden synthetische Testdaten und einen austauschbaren CLI-Runner. Das ist kein Nachweis für eine reale Bitwarden- oder Vaultwarden-Anmeldung.

Das Anwendungsartefakt enthält den Quellcommit in `BUILD.txt` und die EXE-Prüfsumme in `SHA256SUMS.txt`. Testartefakte enthalten `development/report.json`, `packaged/report.json` sowie jeweils `page.png` und `ui.png`. GitHub bewahrt diese Artefakte 14 Tage auf.

Offen bleiben echte Bitwarden-/Vaultwarden-Logins, Zwei-Faktor-Flows, reale Synchronisierung und Schreiben mit Testkonten, signierte Distribution/Updates und breitere Dienst-/Accessibility-Prüfungen. Chromium-Kompatibilität ersetzt keine getesteten Anmelde- oder DRM-Flows jedes Dienstes.
