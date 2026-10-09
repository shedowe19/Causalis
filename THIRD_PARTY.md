# Drittanbieter

## Electron / Chromium

Electron 44.7.0: https://github.com/electron/electron/releases/tag/v44.7.0

Electron steht unter MIT; Chromium und seine Komponenten besitzen zusätzliche Lizenzen. `LICENSE`, `LICENSES.chromium.html`, Ressourcen und Locale-Dateien der offiziellen Electron-Distribution bleiben im Windows-Paket erhalten.

Der integrierte Downloader verwendet Electron's `@electron-internal/extract-zip` 1.0.5. Diese Veröffentlichung deklarierte BSD-2-Clause in den Metadaten, enthielt aber keinen Lizenztext. Upstream hat denselben Programmcode im unmittelbar folgenden Commit ausdrücklich unter MIT gestellt und den vollständigen Lizenztext ergänzt. Der [Quellvergleich](https://github.com/electron/extract-zip/compare/b83e459fd04c53b0a1c8438a6792df8f64be47fc...3c33b76429ebd9bf724bcfc32d3e8fae7eeb3e82) zeigt ausschließlich diese Lizenzänderungen; Programmcode, Builddateien und Abhängigkeits-Lockdateien blieben identisch.

Die unveränderten Laufzeitdateien und der vollständige originale MIT-Text samt historischer BSD-Deklarationsnotiz in [licenses/extract-zip.txt](licenses/extract-zip.txt) werden mitgeliefert. Die Copyright-Zeile stammt aus der [Upstream-Lizenz am Relizenzierungscommit](https://github.com/electron/extract-zip/blob/3c33b76429ebd9bf724bcfc32d3e8fae7eeb3e82/LICENSE); eine BSD-Copyright-Zeile wurde nicht ergänzt. Die historischen Paketmetadaten können weiterhin BSD-2-Clause anzeigen.

## Bitwarden

Der Browser-Download enthält keine Bitwarden-Binärdatei. „Bitwarden-Client installieren“ lädt beim Nutzer die unveränderte offizielle OSS-Ausgabe herunter:

- Release: https://github.com/bitwarden/clients/releases/tag/cli-v2026.9.1
- Archiv: https://github.com/bitwarden/clients/releases/download/cli-v2026.9.1/bw-oss-windows-2026.9.1.zip
- Quellcommit: https://github.com/bitwarden/clients/tree/8246ae9c9a484a0a69f8b27203034555fb872523
- Buildrezept: https://github.com/bitwarden/clients/blob/8246ae9c9a484a0a69f8b27203034555fb872523/.github/workflows/build-cli.yml
- Lizenzzuordnung: https://github.com/bitwarden/clients/blob/8246ae9c9a484a0a69f8b27203034555fb872523/LICENSE.txt
- GPL-Text: https://github.com/bitwarden/clients/blob/8246ae9c9a484a0a69f8b27203034555fb872523/LICENSE_GPL.txt

Die OSS-Ausgabe wird über einen getrennten CLI-Prozess verwendet. Komponenten unter `bitwarden_license` werden von Causalis weder eingebaut noch verteilt. Bitwarden und Vaultwarden sind keine mit Causalis verbundenen Organisationen.
