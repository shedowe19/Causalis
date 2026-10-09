# Prüfung von Causalis 0.3

Am 9. Oktober 2026 wurde die Browserbasis auf Chromium/Electron umgestellt. Ergebnisse der früheren eigenen Engine gelten nicht als Nachweis für diesen Stand.

Die Node-Regressionstests prüfen tatsächliche Eingabegrenzen, CLI-Umgebungen und stdin, Server-/URI-Zuordnung, zeitlich begrenzte Auswahl, Sperren während laufender Abrufe, Workspace-Wechsel und Formularprüfungen. Die eigene UI sowie der Main-Prozess wurden zusätzlich unabhängig auf IPC- und Geheimnisgrenzen geprüft.

Die native Windows-Prüfung erfolgt im GitHub-Workflow. Maßgeblich sind der Quellcommit in `BUILD.txt`, die Job-Ergebnisse und `report.json` in den zugehörigen Artefakten. Dieses Dokument behauptet vor Abschluss des aktuellen Runs keinen Windows-Erfolg.

Der Smoke-Test startet sowohl Electron aus dem Entwicklungsstand als auch die fertig gepackte EXE. Er verwendet Chromium selbst für CSS Grid, Flexbox, SVG, PNG, Canvas, JavaScript, Formulare, Navigation und Screenshots. Er prüft UI-/Website-API-Isolation, fremde und veraltete Autofill-Ziele sowie das Fehlen einer Chrome-Passwortdatenbank im frischen Testprofil.

Offen bleiben echte Bitwarden-/Vaultwarden-Logins, Zwei-Faktor-Flows, reale Synchronisierung und Schreiben mit Testkonten, signierte Distribution/Updates und breitere Dienst-/Accessibility-Prüfungen. Chromium-Kompatibilität ersetzt keine getesteten Anmelde- oder DRM-Flows jedes Dienstes.
