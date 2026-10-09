# Causalis für Windows

Diese native C++20-Oberfläche verwendet die eigene HTML/CSS-Engine und Windows GDI. Sie bindet keine Chromium-, Gecko-, Edge- oder WebView-Engine ein. Die kleine eigene Skriptlaufzeit ist bewusst kein vollständiges JavaScript/DOM.

## Bauen

Voraussetzung: Visual Studio 2022 mit dem aktuellen Windows SDK und „Desktopentwicklung mit C++“. In einer **x64 Native Tools Command Prompt**:

```powershell
powershell -ExecutionPolicy Bypass -File windows\build.ps1
.\build-windows\causalis.exe
```

Alternativ über die CMake-Ziele des Hauptprojekts bauen. Der Windows-Host wurde in der Linux-Entwicklungsumgebung nicht ausgeführt; ein Windows-Smoke-Test ist vor Alltagseinsatz erforderlich.

## Bedienung

- **Strg+O** öffnet eine lokale UTF-8-HTML-Datei bis 2 MiB. Relative Links auf lokale `.html`/`.htm`-Dateien funktionieren. UNC-/Gerätepfade sind ausgeschlossen.
- **Strg+T / Strg+W** erzeugt/schließt echte Dokument-Tabs. Zurück/Vorwärts navigiert innerhalb von bis acht gespeicherten Seiten je Tab; gespeicherte Seiten sind auf 8 MiB je Tab begrenzt. **Strg+L** fokussiert die Adresse.
- **Privat / Arbeit / Homelab** besitzen getrennte Tabs, Verläufe, Lesezeichen und CLI-Appdaten. Es existiert noch kein Cookie-/Kontenmodell; dies sind Dokument-Arbeitsbereiche.
- **Lesen** wechselt zwischen Originaldarstellung und passivem Lesedokument.
- **Warum?** öffnet die Seiteninspektion. Ein Klick auf den Inhalt zeigt Quell-ID, Zeichenbefehle, Position, Schriftgröße und Linkziel. Im Inspektionsmodus dienen Klicks zur Auswahl.
- **Strg+F** sucht innerhalb einzelner gezeichneter Textläufe. Enter oder „Suchen“ springt zum nächsten Treffer. Treffer werden gelb hinterlegt.
- **Seitenstand** erzeugt eine passive `.causalis`-Datei ohne Skripte, Formulare oder aktive Ressourcen. Datei → Seitenstand öffnen stellt sie wieder her. **Vergleichen** nutzt den begrenzten Textvergleich des gemeinsamen Checkpoint-Moduls.
- **Datei → Projekt speichern** speichert die aktuellen Dokumentquellen und die ausgewählte Registerkarte im Format `.causalis-project`, bis 16 MiB. Projekt öffnen ersetzt die Tabs des aktuellen Arbeitsbereichs erst nach vollständiger Validierung. Es lädt keine URLs automatisch und die eingebetteten Dokumente bleiben passiv: auch das spätere lokale Skriptmenü kann keine importierten Quellen ausführen. Zum erneuten Ausführen eine originale lokale HTML-Datei ausdrücklich öffnen. Der Browser sammelt hierfür keine CLI- oder Tresorgeheimnisse. Das Format enthält jedoch die **originalen HTML-Quellen** einschließlich vorhandener Skripte und Attribute; darin können private Seitentexte oder vom Dokument selbst enthaltene Kontodaten stehen. Für einen bereinigten Stand die passive `.causalis`-Datei verwenden.
- **☆** speichert ein Lesezeichen; Datei → Lesezeichen öffnet die Auswahl. Lesezeichen stehen getrennt unter `%LOCALAPPDATA%\Causalis\workspaces\<Arbeitsbereich>\bookmarks.txt`.

## Eigene lokale Skriptlaufzeit

„Engine → Lokale Skripte einmal ausführen“ verwendet den gemeinsamen `render_page`-Ablauf mit einem gemeinsamen Budget von 100.000 Schritten. Nur explizit geöffnete lokale Dateien und das eingebaute Beispiel können ausgeführt werden. Unterstützte Änderungen (`#id`-Text, freigegebene Attribute und visuelle Stile) werden in das eigene Dokumentmodell übernommen. Konsole und Diagnosen erscheinen unter „Warum?“.

HTTPS-Dokumente und passive Seitenstände dürfen keine Skripte ausführen. Es gibt keine Netzwerk-, Datei-, Tresor-, Timer- oder vollständige DOM-API im Skriptkontext.

## Experimenteller HTTPS-Dokumentzugriff

HTTPS ist beim Start ausgeschaltet. Der Schalter **HTTPS-Versuch** erklärt die fehlende Prozess-Sandbox und aktiviert den Zugriff nur für die laufende Sitzung. Unterstützt werden ausdrücklich eingegebene `https://`-Adressen und relative HTTPS-Links. Die eigene Engine lädt keine Bilder, Stylesheets, externen Skripte, Frames oder andere Subressourcen.

Der WinHTTP-Arbeiter:

- läuft abseits der UI; Antworten tragen eindeutige Tab-IDs und Navigationsgenerationen;
- verwirft Antworten auf geschlossene Tabs oder überholte Navigationen;
- deaktiviert Cookies, automatische Authentifizierung und automatische Weiterleitungen;
- prüft jede Weiterleitung erneut auf HTTPS ohne eingebettete Zugangsdaten; maximal fünf Weiterleitungen;
- verwendet die normale Windows-TLS-Zertifikatsprüfung und keine Ignore-Flags;
- akzeptiert HTML/XHTML, unkomprimiert, als UTF-8 bis 2 MiB;
- besitzt fünf Sekunden je WinHTTP-Phase und eine 30-Sekunden-Übertragungsgrenze;
- verhindert beim Schließen Heap-/HWND-Zeigerübergaben; die gemeinsame Queue besitzt alle Ergebnisse.

Stoppen/Schließen markiert Anfragen als verworfen. Eine gerade blockierende synchrone WinHTTP-Phase kann noch bis zu ihrem Timeout auslaufen. Die App ist ein experimenteller Dokumentbrowser ohne Prozess-Sandbox, Formulare oder Passwortfelder. Keine echten Anmeldungen damit durchführen.

## Bitwarden / Vaultwarden

Das Tresormenü verwendet ausschließlich einen ausdrücklich ausgewählten offiziellen `bw.exe`-Client. Jede Operation läuft in einem eigenen CLI-Prozess außerhalb der Seiten- und Skriptverarbeitung. Die Oberfläche wartet darauf asynchron.

1. Tresor → Offiziellen bw.exe-Client auswählen.
2. Server: Bitwarden USA / Europa auswählen, oder eine gültige Vaultwarden-HTTPS-URL in die Adresszeile eingeben und **Server: Vaultwarden-URL aus Adresszeile** wählen.
3. Tresorstatus, Sperren oder Synchronisieren verwenden. Der eigene CLI-Appdatenordner lautet `%LOCALAPPDATA%\Causalis\workspaces\<Arbeitsbereich>\BitwardenCLI`.
4. „Bitwarden-Desktop-App öffnen“ startet den registrierten `bitwarden:`-Protokollhandler. Anmeldung und Entsperrung erfolgen außerhalb des Browsers.

Die CLI-Anmeldung für den getrennten Ordner erfolgt derzeit manuell außerhalb der App. Die Desktop-App und CLI teilen ihren Entsperrstatus nicht automatisch. Der Host fordert keine Passwörter an, liest keine Tresoreinträge und implementiert noch kein Autofill oder Passkey. Statusausgaben werden auf den freigegebenen Status-/Server-/Synchronisationsumfang reduziert.

## Automatischer nativer Test für GitHub Actions

Der Windows-Client besitzt einen nicht interaktiven Prüfmodus:

```powershell
.\build-windows\causalis.exe --smoke-test
```

Für einen zuverlässigen Prozess-Wait im CI den CTest-Test `windows-native-smoke` auf einem Windows-Runner verwenden. Der Modus erzeugt ein echtes, unsichtbares Win32-Fenster mit allen nativen Controls. Er prüft die eigene Darstellung und GDI-Textmessung, Tab-Kommandos wie bei Tastenkürzeln, eine temporäre lokale UTF-8-Datei, explizite Skriptänderungen, Suche, Leseansicht, klickbare Quelleninspektion, Größenänderung, Offscreen-GDI-Pixel, Zurück/Vorwärts und die Trennung der Arbeitsbereiche. Er prüft außerdem, dass Netzwerk-, passive und importierte Dokumente keine lokalen Skripte ausführen dürfen.

Es gibt dabei keine Dialoge, HTTPS-Anfragen, Tresorzugriffe oder Änderungen an echten Arbeitsbereichsspeichern. Das Fenster und die temporäre Datei werden wieder entfernt. Ein echter Testbericht entsteht erst bei Ausführung in Windows.

Exitcodes: **0** bestanden, **1** Initialisierung fehlgeschlagen, **2** native Prüfung fehlgeschlagen. `causalis-smoke-report.txt` wird im Arbeitsverzeichnis geschrieben; verfügbare geerbte Standardausgaben erhalten denselben Bericht. Der Test prüft die verborgene native UI und GDI-Zeichnung; er ersetzt keine visuelle Prüfung eines sichtbaren Fensters, Netzwerk-Kompatibilitätstests oder echte Bitwarden-Anmelde-/Autofilltests.

## Windows-Smoke-Test

Nach einem erfolgreichen Build prüfen:

1. Beispiel öffnen, Fenster vergrößern/verkleinern, scrollen, Tabs erstellen und schließen.
2. Lokale `samples`-Dateien öffnen; Zurück/Vorwärts, Lesen, Suche und Inspektion prüfen.
3. Lokales Beispielskript explizit ausführen; Änderung und Konsole prüfen.
4. Seitenstand speichern/öffnen/vergleichen und Projekt speichern/öffnen.
5. Arbeitsbereiche wechseln; Lesezeichen nur im jeweiligen Bereich sehen.
6. HTTPS-Versuch explizit aktivieren, `https://example.com/` laden, während des Ladens Tab schließen/wechseln/neu navigieren. Keine Antwort darf die falsche Seite ersetzen. HTTP- und Credential-URLs müssen abgelehnt werden.
7. Mit separat eingerichtetem offiziellen CLI Status/Sperren testen. Fehlender Client, gesperrter Tresor, fehlerhafter Server und Zeitüberschreitung müssen als Fehler erscheinen; niemals Geheimnisse in UI/Projekten.
