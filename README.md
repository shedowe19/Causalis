# Causalis 0.3 — Chromium und Bitwarden

Causalis ist ein Windows-Browser auf Chromium-Basis mit Electron 44.7.0. Webseiten verwenden Chromium und V8: Bilder, moderne CSS-Layouts und JavaScript werden vom regulären Webkern verarbeitet. Die frühere eigene Engine ist durch diesen Ansatz ersetzt; ihr Quellstand bleibt in der Git-Historie erhalten.

**Bitwarden/Vaultwarden ist der einzige Passwortmanager.** Causalis verwendet den offiziellen Bitwarden-CLI als Tresor-Backend und baut weder einen Chromium-Passwortspeicher noch eine eigene Passwortdatenbank ein. Es ist ein unabhängiges Projekt und keine offizielle Bitwarden-Anwendung.

## Windows starten

Unter [GitHub Actions](https://github.com/shedowe19/Causalis/actions) einen erfolgreichen **Chromium Windows build and test**-Run öffnen, das Artefakt `Causalis-Chromium-windows-latest-x64` herunterladen, vollständig entpacken und `Causalis.exe` starten. Alle Dateien im entpackten Ordner werden benötigt. Das Paket ist eine unsignierte Entwicklungsanwendung ohne Installer und automatische Updates.

Im Tresorbereich **Bitwarden-Client installieren** anklicken. Causalis lädt die unveränderte offizielle OSS-Ausgabe `2026.9.1` von Bitwarden herunter, prüft die festgelegte SHA-256-Prüfsumme und installiert sie in seinem Anwendungsdatenordner. Der Client wird nicht in unserem Downloadpaket weiterverteilt. Alternativ kann eine selbst geprüfte offizielle `bw.exe` ausgewählt werden.

1. Bitwarden USA, Bitwarden Europa oder den HTTPS-Server deines Vaultwarden wählen. Ein Serverwechsel setzt einen abgemeldeten Tresor voraus.
2. Mit E-Mail, Masterpasswort und gegebenenfalls Zwei-Faktor-Code anmelden. Alternativ mit persönlichem API-Schlüssel anmelden und mit Masterpasswort entsperren.
3. Eine HTTPS-Anmeldeseite öffnen, passende Einträge laden und einen Eintrag ausdrücklich zum Einfüllen auswählen. Der Browser füllt nur ein eindeutiges sichtbares Anmeldeformular in der Hauptseite aus und sendet es nicht ab.
4. Neue Zugangsdaten können im Tresorbereich ausdrücklich gespeichert werden; der Passwortgenerator verwendet kryptografische Zufallswerte.

## Funktionen

- Tabs, Suche/Adresszeile, Zurück/Vorwärts, Neuladen und Downloads mit Dateiauswahl.
- Persönlich, Arbeit und Homelab mit getrennten Website-Sessions und getrennten Bitwarden-CLI-Profilen.
- Privater Arbeitsbereich mit einer nicht persistenten Website-Session. Der darin verwendete **verschlüsselte CLI-Tresorcache bleibt auf dem Gerät**, bis du ihn separat entfernst.
- Integrierter Tresor: Anmeldung, Entsperren, Synchronisieren, Sperren, Abmelden, genaue HTTPS-Origin-Zuordnung, Einfüllen, neue Logins und Passwortgenerator.
- Automatische Tresorsperre nach fünf Minuten ohne relevante Tresoraktivität, beim Arbeitsbereichswechsel, Windows-Sperren, Suspend und Beenden.
- Websites besitzen keinen Node-Zugriff, keinen privilegierten Preload und keinen Zugriff auf die Tresor-IPC. Chromium-Sandbox, Kontextisolation, Websicherheit und Zertifikatsprüfung bleiben aktiviert.

## Entwicklungsstand und Grenzen

Die Bitwarden-Browsererweiterung wird nicht eingebettet: Electron implementiert nur einen Teil der Chrome-Erweiterungs-APIs. Unsere Oberfläche greift auf den offiziellen nativen Client zu. Biometrie, Passkeys, Organisationseinträge mit erneuter Masterpasswortabfrage, SSO-Oberflächen und automatisches Speichern beim Absenden einer Webseite sind noch nicht integriert. FIDO2/Duo für den CLI-Login erfordern die API-Schlüssel-Alternative. Autofill beschränkt sich bewusst auf die **genaue HTTPS-Origin**, nicht auf alle Subdomains derselben Domain; fremde Frames und fremde Formularziele erhalten keine Zugangsdaten.

Website-Berechtigungen für Kamera, Mikrofon, Standort und Benachrichtigungen sind derzeit standardmäßig verweigert. DRM, Browsererweiterungen und interne PDF-Viewer sind keine zugesicherte Funktion dieses Stands. Es gibt noch keinen signierten Installer oder Updatekanal. Vor echter Tresornutzung sind Login/Sync/Speichern gegen deinen Server praktisch zu prüfen; automatisierte Tests verwenden dafür synthetische Zugangsdaten.

## Entwickeln und prüfen

Node.js 24 und npm verwenden:

```powershell
npm ci
npm run check
npm test
npm start
```

Windows-Paket und echte Chromium-Prüfung:

```powershell
npm run prepare:client
$env:CAUSALIS_CI_BW_PATH = (Resolve-Path vendor/client/bw.exe).Path
$env:CAUSALIS_REPORT_DIR = Join-Path $PWD 'test-results/development'
npm run smoke
npm run package:windows
```

Der Workflow prüft zusätzlich die **fertige EXE** und lädt nur bei Erfolg das komplette Anwendungsartefakt hoch. Berichte und echte Chromium-Screenshots erscheinen als separate Artefakte. Der Prüfmodus verwendet ausschließlich temporäre Profile; er berührt keine echten Benutzerkonten.

Details: [Architektur](docs/ARCHITECTURE.md), [Tresorgrenzen](docs/VAULT.md), [CI-Prüfung](docs/CI.md), [Prüfstand](VALIDATION.md).

Der Projektcode steht unter GPL-3.0-only. Electron enthält eigene MIT-/Chromium-Drittanbieterhinweise, die im Paket erhalten bleiben. Bitwardens unveränderter OSS-Client wird direkt von dessen offizieller Downloadquelle bezogen; Quellen und Lizenzzuordnung stehen in [THIRD_PARTY.md](THIRD_PARTY.md).
