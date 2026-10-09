# Drittanbieter

## Electron / Chromium

Electron 44.7.0: https://github.com/electron/electron/releases/tag/v44.7.0

Electron steht unter MIT; Chromium und seine Komponenten besitzen zusätzliche Lizenzen. `LICENSE`, `LICENSES.chromium.html`, Ressourcen und Locale-Dateien der offiziellen Electron-Distribution bleiben im Windows-Paket erhalten.

Der integrierte Downloader verwendet Electron's `@electron-internal/extract-zip` 1.0.5 (BSD-2-Clause). Dessen unveränderte Laufzeitdateien und Lizenzhinweise bleiben im Paket erhalten.

## Bitwarden

Der Browser-Download enthält keine Bitwarden-Binärdatei. „Bitwarden-Client installieren“ lädt beim Nutzer die unveränderte offizielle OSS-Ausgabe herunter:

- Release: https://github.com/bitwarden/clients/releases/tag/cli-v2026.9.1
- Archiv: https://github.com/bitwarden/clients/releases/download/cli-v2026.9.1/bw-oss-windows-2026.9.1.zip
- Quellcommit: https://github.com/bitwarden/clients/tree/8246ae9c9a484a0a69f8b27203034555fb872523
- Buildrezept: https://github.com/bitwarden/clients/blob/8246ae9c9a484a0a69f8b27203034555fb872523/.github/workflows/build-cli.yml
- Lizenzzuordnung: https://github.com/bitwarden/clients/blob/8246ae9c9a484a0a69f8b27203034555fb872523/LICENSE.txt
- GPL-Text: https://github.com/bitwarden/clients/blob/8246ae9c9a484a0a69f8b27203034555fb872523/LICENSE_GPL.txt

Die OSS-Ausgabe wird über einen getrennten CLI-Prozess verwendet. Komponenten unter `bitwarden_license` werden von Causalis weder eingebaut noch verteilt. Bitwarden und Vaultwarden sind keine mit Causalis verbundenen Organisationen.
