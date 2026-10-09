# Eigenständige Engine: Architektur und Meilensteine

## Eigener Kern

Der HTML-Parser, die Dokumentstruktur, CSS-Verarbeitung, Layoutregeln, Display-Listen, Webrichtlinien und eine begrenzte Skriptlaufzeit werden als eigene Komponenten entwickelt. Betriebssystemgrafik, bewährte TLS-/Kryptobibliotheken, Schriftformung und Decoder können Infrastruktur liefern. Ihre Verwendung ersetzt nicht den eigenen Webkern.

Version 0.2 enthält einen originalen Interpreter mit JavaScript-Syntax, aber keine vollständige ECMAScript-Implementierung und keine andere Browser-Engine. Die Bindung ist absichtlich eng: sichere Änderungen an #id-Zielen; kein Zugriff auf Netzwerk, Dateien oder Tresore.

## Geplante Prozessgrenzen

| Prozess/Komponente | Aufgabe und Grenze |
| --- | --- |
| Browser-Oberfläche | Tabs, Projekte, Identitäten, Nutzerentscheidungen; keine Ausführung von Seiten-Skripten |
| Renderer je Isolationskontext | Dokument, CSS, Layout, Paint, später Skripte; eingeschränkter Zugriff auf Dateien und Netzwerk |
| Netzwerk-Broker | HTTPS, Redirects, Zertifikate, Fetch-/CORS-Regeln und Ressourcenbudgets |
| Storage-Broker | Origin- und Profilzuordnung für Cookies, Cache und später Webspeicher |
| Zugangsdaten-Broker | Clientseitige Tresoroperationen außerhalb von Seiten- und KI-Prozessen |
| Inspector/Diagnose | Ereignisse, Herkunftsdaten und verständliche Erklärungen mit Grenzen |

Version 0.2 läuft mit Oberfläche, Renderer, Dokumentadapter und Interpreter im selben Prozess. HTTPS läuft auf einem begrenzten Arbeiterthread; die offizielle Bitwarden-CLI läuft als eigener normaler Benutzerprozess. Threads liefern keine Sicherheitsisolation. Die Tabelle beschreibt das Ziel, keinen bestehenden Sandboxschutz.

Windows AppContainer und Broker-Schnittstellen bleiben Voraussetzung für eine Freigabe als Alltagsbrowser mit aktiven Webseiten. Der jetzige HTTPS-Dokumentversuch ist standardmäßig ausgeschaltet, führt keine Seitenskripte aus und hat dennoch keine Prozesssandbox. Das Sicherheitsmodell braucht Tests für Prozessgrenzen, Navigation, Originwechsel, Dateien, Frames und Berechtigungen.

## Bestehender Datenfluss in 0.2

Lokale HTML-Daten gehen an den Dokumentadapter und den eigenen Renderer. Nur ein ausdrücklicher Hostbefehl führt unterstützte klassische Inline-Skripte aus. Der Interpreter liefert eine Transaktion mit Text-/Attribut-/Stiländerungen; der Host wendet sie auf eine Kopie an. Bei Größen- oder Arbeitsbudgetüberschreitung wird die gesamte Dokumentänderung verworfen. Remote-Dokumente und importierte Projekte erhalten keine lokale Skriptberechtigung.

Der gemeinsame Seitenpfad akzeptiert höchstens 2 MiB Quelle, 256 Änderungen und 16 MiB kumulierte Dokumentbytes für erneute Mutationsanalysen. Der Interpreter hat eigene Quellen-, Syntax-, Tiefen-, Ausführungs- und Ausgabelimits. Der Dokumentadapter begrenzt Attribute pro Element und insgesamt. Die Rendererlimits bleiben zusätzlich wirksam; ein Budget ist kein Konformitäts- oder Sicherheitsnachweis.

Seitenstände entstehen aus der erzeugten passiven Leseansicht. Der Decoder prüft das Längenformat und bereinigt die gespeicherte Quelle erneut. Der Vergleich nutzt Textzeilen bis 2048 Zeilen und 4096 Byte pro Zeile; sonstige Textänderungen erhalten einen Hinweis. Er rekonstruiert weder JavaScript-Zustand noch Serveraktionen. Projektdateien halten dagegen unveränderte Dokumentquellen und werden beim Import passiv behandelt.

Navigationsergebnisse tragen eindeutige Tab-IDs und Generationen. Die UI übernimmt nur noch gültige Antworten aus einer besitzenden Ergebnisqueue. Die Windows-Quelldateien wurden unabhängig geprüft und am 2026-10-09 mit MSVC auf `windows-latest` und `windows-2022` nativ kompiliert. Auf beiden Runnern bestanden alle sieben CTest-Ziele einschließlich des automatischen nativen UI-/GDI-Smoke-Tests sowie neun CLI-Beispielmodi und zwei Prüfungen ungültiger CLI-Eingaben. Der [Windows-Run](https://github.com/shedowe19/Causalis/actions/runs/37914071885) prüft Quellstand `3b9d6291d9b6ac8b13e7cc48092f1c68d512def6`. Er enthält keine echten HTTPS- oder Tresoroperationen und ersetzt keine sichtbare UI- oder Accessibility-Prüfung.

## M0: Nachweisbarer eigener Renderer

Aktuelles Paket: begrenzter HTML-/CSS-Umfang, Textfluss, Display-Liste, Diagnosen, Dokumentadapter, eigener Interpreter, JSON-CLI und erweiterter nativer Windows-Host mit erfolgreich gebautem Windows-x64-Artefakt. Die portablen Algorithmen werden separat und integriert getestet; der Windows-Host hat zusätzlich den beschriebenen automatischen nativen Smoke-Test bestanden. Weitere Plattform-, Netzwerk- und Kompatibilitätsprüfungen bleiben offen.

Erfolgskriterium: Die eigene Implementierung erzeugt für erklärte Testdokumente reproduzierbare Struktur- und Layoutausgaben; malformed und begrenzte Eingaben erzeugen keine undefinierten Zugriffe in den ausgeführten Prüfungen. Das ist kein allgemeiner Sicherheitsnachweis.

## M1: Dokumentbrowser mit klaren Grenzen

Tabs, einfache Navigation/History, Hit-Testing, eine passive Leseansicht und Quellinspektion bestehen als Quellimplementierung. Weitere Arbeit: HTML-Tokenizer und Baumkonstruktion näher an den Standards, vollständige DOM-Schnittstellen, CSS-Syntax/Kaskade, Textauswahl und Formulare, realistische Schriftformung, Bilder, Prozess-/Origin-/Netzwerkisolierung und erweitertes Herkunftsprotokoll.

Erfolgskriterium: Ein dokumentierter Satz realer, überwiegend statischer Seiten und ausgewählter Web Platform Tests wird erfolgreich verarbeitet. Unvollständig unterstützte APIs bleiben kenntlich.

## M2: Eigene JavaScript-Laufzeit und DOM-Ereignisse

Lexikalische Analyse, Parser, primitive Werte, Kontrollfluss, benannte Funktionen mit lexikalischen Umgebungen und begrenzte DOM-Transaktionen bestehen. Sie bilden nur eine Teilmenge ab. Weitere Arbeit: vollständiges ECMAScript-Werte-/Objektmodell, Speicherverwaltung, Exceptions, Sprachsemantik, Module, Promises, Event Loop, DOM-Ereignisse und definierte Web-APIs. Optimierung folgt nach überprüfbaren Semantiken.

ECMAScript und Web-APIs werden getrennt geprüft: Test262 beschreibt Sprachkonformität, Web Platform Tests die Webplattform. Ein kleiner Interpreter ist noch kein V8-Ersatz und macht nicht automatisch moderne Webapps lauffähig.

## M3: Breitere moderne Webseiten

Flexbox/Grid, SVG, Fonts, Fetch, Formulare, Webspeicher, Accessibility, Medien und weitere APIs nach Priorität und Kompatibilitätstests. Unterschiede sichtbar verfolgen. WebGL, WebRTC, Streaming-DRM und bank-/dienstspezifische Anforderungen benötigen gesonderte Integrationen und reale Diensttests.

## M4: Zugangsdatenintegration

Die Bitwarden-Erweiterung erwartet Browser- und Erweiterungs-APIs, die eine neue Engine nicht automatisch besitzt. Der aktuelle schmale Windows-Broker startet eine separat installierte offizielle CLI für Status, Serverwahl, Sync, Lock und Logout. Er beschafft keine Geheimnisse und ist noch kein vollständiger Passwortmanager. Details stehen in [VAULT.md](VAULT.md). Die spätere Clientbasis für Login, Entsperrung und Autofill wird anhand Wartbarkeit und Komponentenlizenzen gewählt. Vor einer Wiederverwendung werden GPL-Komponenten und gesondert lizenzierte Bereiche getrennt geprüft.

Der Broker muss Bitwarden-Cloudregionen und aktuelle Vaultwarden-Versionen praktisch testen. Die Bitwarden Public API für Organisationsverwaltung ist keine persönliche Passwortabruf-API. Ein CLI-Prototyp hätte außerdem nicht automatisch alle Anmelde-, Passkey- und Entsperrfunktionen eines vollständigen Clients.

Keine selbst erfundene Tresorverschlüsselung: Clientprotokolle, Schlüsselableitung, Synchronisierung und Integrität werden nach den dokumentierten Verfahren umgesetzt oder aus geeigneten, geprüften Clientkomponenten übernommen. Vor echter Nutzung gehören unabhängige Prüfung und Kompatibilitätstests dazu.

Vor dem Einfüllen werden Profile, Zielnavigation, Top-Level-Origin und Frame-Origin erneut geprüft. Renderer und KI können keine beliebigen Tresoreinträge lesen. Das Einfüllen gibt Zugangsdaten jedoch an die gewählte Website weiter; deren Skripte können ihre eigenen Formulare lesen.

Windows Hello, WebAuthn, Passkeys, Zwei-Faktor-Anmeldung und Organisationen haben getrennte Erfolgskriterien. Weder UI-Design noch ein vorhandener Windows-WebAuthn-Aufruf belegen diese Kompatibilität.

## M5: Produktfunktionen und veröffentlichbarer Windows-Browser

Dokumentprojekte, passive Seitenstände, begrenzter Vergleich, Tabs, Lesezeichen und Tastaturbefehle bestehen als Quellimplementierung. Weitere Arbeit: Ressourcenübersicht, nachvollziehbare Abläufe, Downloads, sichere Importe, Updates, Crashwiederherstellung, Installer, Signierung, Einstellungen, vollständige Tastatur-/Accessibility-Bedienung und Mehrsprachigkeit.

Die Meilensteine sind Reihenfolge und Abnahmekriterien, keine zugesagten Fertigstellungstermine. Vollständige Alltagskompatibilität ist ein langfristiges Großprojekt.

## Normative und primäre Entwicklungsquellen

- HTML und Fehlerbehandlung: https://html.spec.whatwg.org/multipage/parsing.html
- DOM: https://dom.spec.whatwg.org/
- CSS-Syntax: https://www.w3.org/TR/css-syntax-3/
- CSS-Kaskade: https://www.w3.org/TR/css-cascade-5/
- ECMAScript: https://tc39.es/ecma262/
- Fetch/Origins/CORS: https://fetch.spec.whatwg.org/
- Web Platform Tests: https://web-platform-tests.org/
- Test262: https://github.com/tc39/test262
- Windows AppContainer: https://learn.microsoft.com/en-us/windows/win32/secauthz/appcontainer-isolation
- Windows WebAuthn: https://learn.microsoft.com/en-us/windows/win32/api/webauthn/
- Bitwarden-Serverwahl: https://bitwarden.com/help/change-client-environment/
- Vaultwarden: https://github.com/dani-garcia/vaultwarden
- Bitwarden API-Einordnung: https://bitwarden.com/help/bitwarden-apis/
- Bitwarden-Komponentenlizenzen: https://github.com/bitwarden/clients/blob/main/LICENSE.txt
