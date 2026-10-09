# Causalis Browser 0.2.0

Ein Windows-Dokumentbrowser mit einer selbst geschriebenen Engine, eigener experimenteller Skriptlaufzeit und nativer Oberfläche. Dieses zusammenhängende Quellprojekt enthält Beispiele, Tests und Windows-Bauskripte. Es ist noch kein vollständig kompatibler Alltagsbrowser und enthält keine geprüfte Windows-EXE.

HTML-Verarbeitung, einfache CSS-Kaskade, Textlayout, Display-Liste, Dokumentadapter und Skriptinterpreter entstehen in unserem eigenen C++20-Code. Chromium, Blink, Gecko, WebKit, CEF, WebView und fremde JavaScript-Interpreter werden nicht eingebettet. Windows GDI liefert die Grafikausgabe und WinHTTP den optionalen HTTPS-Transport.

## Enthaltene Funktionen

| Bereich | Stand in diesem Paket |
| --- | --- |
| Eigene Engine | Begrenztes HTML/CSS, Block-/Inline-Textfluss, Links, Herkunfts-IDs und Diagnosen; portabel getestet |
| Eigene Skriptlaufzeit | JavaScript-Syntax mit Variablen, Ausdrücken, Schleifen, benannten Funktionen, Konsole und sicheren #id-Änderungen; portabel getestet; ausdrücklich lokale Ausführung |
| Dokumentmodell | Quelltext erhalten, Titel/Text inspizieren, begrenzte sichere Mutationen und passive Leseansicht; portabel getestet |
| Seitenstände | Passive .causalis-Dateien, erneute Bereinigung beim Import, begrenzter Textvergleich mit Hinweis auf sonstige Textänderungen; portabel getestet |
| Windows-Oberfläche | Tabs, Zurück/Vorwärts, drei Arbeitsbereiche, Lesezeichen, Suche, Leseansicht, Quellinspektion, Seitenstände und Projektdateien; Quellcode geprüft, Windows-Ausführung offen |
| HTTPS | Optionaler WinHTTP-Dokumentzugriff, beim Start aus; keine aktiven Seiten-Skripte oder Subressourcen; Windows-Ausführung offen |
| Bitwarden/Vaultwarden | Begrenzte Verwaltung über separat installierte offizielle bw.exe: Serverwahl, Status, Sync, Sperren; portable Richtlinien getestet, Windows-Bridge ungetestet |

Die besonderen Funktionen verbinden die Dokumentquelle mit ihrer Darstellung: „Warum?“ zeigt die Herkunft gezeichneter Elemente; dieselbe Quelle erzeugt eine passive Leseansicht und vergleichbare Seitenstände. Arbeitsbereiche halten Dokument-Tabs und CLI-Appdaten getrennt. Ein vollständiges Ursachenprotokoll und isolierte Webidentitäten sind weitere Entwicklungsziele.

## Windows starten

Visual Studio mit C++20-Desktopwerkzeugen und Windows SDK verwenden. In der x64 Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\causalis.exe
```

Alternativ kompiliert windows/build.ps1 das Programm direkt mit MSVC. Bedienung und ein Windows-Prüfablauf stehen in [windows/README.md](windows/README.md). Das eingebaute Beispieldokument funktioniert ohne externe Dateien; samples/script-demo.html zeigt die ausdrücklich ausgelöste Skriptausführung.

Das Projekt enthält auch [.github/workflows/windows.yml](.github/workflows/windows.yml): native Builds und Tests auf windows-latest und windows-2022, ein automatischer Win32-Starttest und EXE-Downloads nach erfolgreicher Prüfung. Einrichtung und tatsächliche Prüfabdeckung stehen in [docs/CI.md](docs/CI.md). Der Workflow ist vorbereitet; ein abgeschlossener GitHub-Run liegt noch nicht vor.

## Portablen Kern prüfen

Mit Python 3 und GCC mit C++20-Unterstützung:

```bash
python3 tools/test_portable.py --build-dir build-portable
./build-portable/causalis-cli samples/start.html 960 > frame.json
./build-portable/causalis-cli samples/script-demo.html 640 --run-scripts --source
./build-portable/causalis-cli samples/start.html 640 --reading
```

Der Prüflauf baut den Kern, sechs Prüfprogramme und die JSON-CLI. Mit --sanitize werden AddressSanitizer und UndefinedBehaviorSanitizer aktiviert. --disable-leak-check ist nur für Umgebungen vorgesehen, in denen LeakSanitizer nicht funktioniert; dann besteht kein Leak-Prüfnachweis.

Die CLI akzeptiert lokale UTF-8-Dokumente bis 2 MiB. Standardmäßig bleiben Skripte aus. --run-scripts führt die unterstützten klassischen Inline-Skripte einmal aus; Module, externe Skripte und Ereignishandler bleiben aus. --source ergänzt die resultierende Dokumentquelle. Konsole und Ausführungsdiagnosen sind getrennte JSON-Felder.

## Grenzen des aktuellen Kerns

HTML-Baumkonstruktion und CSS sind Teilimplementierungen. Der Dokumentadapter ist kein vollständiges HTML5-DOM. Die eigene Skriptsprache ist keine ECMAScript-konforme JavaScript-Engine: keine vollständigen Objekte/Prototypen, Module, Promises, Event Loop oder allgemeinen Web-APIs. DOM-Lesezugriffe kennen nur in derselben Ausführung zuvor geschriebene Werte; fehlende oder inaktive #id-Ziele meldet der Host. Sprachtests und interne Renderertests ersetzen weder Test262 noch Web Platform Tests.

Es fehlen unter anderem interaktive Formulare, Cookies, Webspeicher, Bilder, Flexbox/Grid, Medien, WebGL, WebAuthn, Browsererweiterungen, Installer, signierte Updates und eine Prozesssandbox. Moderne Webapps und echte Anmeldungen sind deshalb kein unterstützter Einsatzzweck dieses Stands. HTTPS ist ein ausdrücklich aktivierbarer Entwicklungsversuch für statische Dokumente.

Die Tresoranbindung beschafft keine Passwörter, führt kein Login/Unlock aus und bietet kein Autofill, Windows Hello oder Passkeys. Die offizielle Erweiterung läuft auf dieser eigenen Engine noch nicht. Einrichtung und genaue Grenzen stehen in [docs/VAULT.md](docs/VAULT.md).

Passive Seitenstände entfernen Skripte, Formulare und aktive Ressourcen, können aber weiterhin private sichtbare Texte und URLs enthalten. Projektdateien speichern die ursprünglichen Dokumentquellen, einschließlich darin vorhandener Skripte und Formulardaten; sie sind kein bereinigtes Austauschformat. CLI-Tresordaten werden nicht darin gesammelt. Projekte starten beim Import keine Skripte und laden keine URLs automatisch.

## Projektstruktur

- src/engine.cpp: eigener Parser, CSS, Textlayout und Paint.
- src/document.cpp, src/script.cpp, src/page.cpp: Dokumentadapter, Interpreter und Ausführungspolitik.
- src/checkpoint.cpp, src/vault_policy.cpp: passive Seitenstände und portable CLI-Richtlinien.
- windows/: native Oberfläche, HTTPS-Helfer und begrenzte CLI-Bridge.
- tests/, tools/test_portable.py: reproduzierbare Prüfung.
- docs/ARCHITECTURE.md, docs/PRODUCT.md: bestehende Architektur und weitere Produktziele.
- VALIDATION.md: tatsächliche Prüfergebnisse und verbleibende Plattformgrenzen.

preview.png zeigt die Display-Liste mit angenäherten Schriftmaßen; es ist kein Windows-Screenshot. tools/preview_frame.py erzeugt solche Debugvorschauen mit Pillow.
