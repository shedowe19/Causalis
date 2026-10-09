# Windows-CI

Der Workflow `.github/workflows/windows.yml` verwendet `runs-on` mit `windows-latest` und `windows-2022`. Er startet bei Push, Pull Request oder manuell. Beide Jobs führen dieselben Schritte aus:

1. Gepinnte Node-/Electron-Abhängigkeiten mit `npm ci` installieren und die Electron-Laufzeit explizit mit `npm run runtime:install` bereitstellen.
2. JavaScript-Syntax und Node-Regressionstests für IPC, Tresor- und Autofill-Grenzen prüfen.
3. Die offizielle OSS-CLI ausschließlich für Tests herunterladen und ihre festgelegte SHA-256 prüfen. Keine Benutzerkonten oder Tresorgeheimnisse werden benötigt.
4. Das echte Electron mit temporärem Profil starten: Chromium-Rendering, Bilder, CSS/JavaScript, Sandbox, Tabs/History, Workspace-Cookies, UI-IPC, originbezogenes Autofill und CLI-Status prüfen.
5. Den Windows-x64-Ordner mit Electron-Fuses vorbereiten und denselben Test an der **fertigen Causalis.exe** wiederholen.
6. Erst bei Erfolg die Anwendung als Artefakt hochladen. Testberichte und echte Screenshots werden auch bei Fehlern hochgeladen, soweit erzeugt.

Die HTTPS-Testseiten entstehen ausschließlich in einer testbezogenen Session für `smoke.invalid`. Globale Flags zum Ausschalten der Sandbox, Zertifikatsprüfung oder Websicherheit sind verboten. Tresor-Anmeldung und Datenabrufe werden mit synthetischen Fixtures geprüft; nur Version/unauthentifizierter Status laufen mit der echten CLI. Das ist kein praktischer Login-/Synchronisierungsnachweis gegen Bitwarden oder einen Vaultwarden-Server.

Das Anwendungsartefakt enthält Electron und den Browser, aber keine Bitwarden-EXE. Der integrierte Download bezieht sie beim Nutzer direkt von Bitwarden. Der vollständige Ordner muss entpackt bleiben. Artefakte werden 14 Tage aufbewahrt; danach einen neuen Run starten.
