# Causalis: außergewöhnlich durch Kontrolle und Zusammenhang

Ziel ist ein eigenständiger Windows-Browser mit selbst entwickeltem Webkern. Er soll Recherche, Alltag, Arbeit, Webapps und technische Projekte verbinden. Volle Kompatibilität mit diesem Anwendungsspektrum entsteht über mehrere Meilensteine.

Das gemeinsame Prinzip: Der Nutzer erkennt, was eine Seite tut, welche Identität sie verwendet und welchen Arbeitsstand der Browser behält. Die folgenden Abschnitte beschreiben das Zielprodukt. Version 0.2 enthält einen Teil davon; die konkrete Funktionsmatrix steht in [README.md](../README.md).

Der aktuelle technische Einstieg umfasst eine Quellinspektion gezeichneter Elemente, eine passive Leseansicht, Seitenstände und begrenzten Textvergleich, getrennte Dokument-Arbeitsbereiche und das Speichern offener Dokument-Tabs. Die CLI-Tresorverwaltung ist noch kein Autofill. Vollständige Ursachenketten, Webidentitäten, Ressourcensteuerung und Automationen bleiben weitere Entwicklungsarbeit.

## 1. Seitenpass: Warum passiert das?

Ein Seitenpass erklärt Netzwerkverbindungen, Berechtigungen, Speicherzugriffe und später Skriptaktivität. Jede Erklärung verbindet eine Aktion mit Ziel-Origin, bekanntem Auslöser, angewendeter Regel und Ergebnis. Ein Nutzer sieht etwa: Ein eingebettetes Modul fordert eine Verbindung zu einer anderen Domain an; eine bestimmte Projektregel hat sie blockiert.

Eine Änderung gilt eng für die betreffende Seite oder den betreffenden Arbeitsbereich. Der Browser zeigt unbekannte Ursachen als unbekannt und erfindet keine Kausalkette. Die Display-Liste und Diagnosen aus dem ersten Kern bilden einen kleinen Einstieg in beobachtbare Engine-Ausgaben; vollständige Herkunftsdaten folgen später.

## 2. Zwei Darstellungen aus demselben Dokument

Die Standardansicht folgt den Webstandards. Eine zusätzliche Lese- und Arbeitsansicht nutzt Dokumentstruktur und Semantik direkt: Überschriften, Landmarks, Quellen, Tabellen und Formulare. Nutzerregeln können Lesbarkeit, Bewegung und Ablenkungen verändern.

Der zweite Pfad läuft in der Engine. Er benötigt kein Seiten-Skript und muss nachvollziehbar zeigen, welche Teile des Dokuments er ausgelassen oder verändert hat. Für komplexe Apps bleibt die Standardansicht erreichbar. Zugänglichkeit und Fokusreihenfolge gehören zu den Prüfungen.

## 3. Passive Seitenstände und Vergleich

Eine Seite lässt sich mit erlaubten Ressourcen lokal als passiver Zustand sichern. Ein späterer Vergleich zeigt veränderte Absätze, Angebote, Dokumentationsstellen oder Layoutbereiche. Das Öffnen eines Zustands führt keine alten Skripte aus.

Diese Funktion nimmt keine Bestellung, Nachricht oder Änderung auf einem Server zurück. Passwortfelder und Tresorgeheimnisse werden nicht gespeichert. Auch andere private Inhalte und kurzlebige URLs müssen vor einer Synchronisierung oder Weitergabe geprüft werden.

## 4. Aufgaben als zusammenhängender Arbeitsstand

Ein Projekt verbindet Tabs, Quellen, Notizen, Vergleichsansichten und einen ausdrücklich gespeicherten nächsten Schritt. Beispiele: ShieldPM entwickeln, eine Reise planen, Unterlagen für die Arbeit vorbereiten.

Ein Projekt wählt eine Identität und ein getrenntes Browserprofil. Cookies, Sitzungen, Storage, Cache und zugeordnete Tresorkonten folgen dieser Auswahl. Ein sichtbarer Indikator zeigt durchgehend, mit welchem Kontext eine Seite arbeitet. Eine gefilterte Tresorsammlung ist eine Bedienhilfe und ersetzt keine Zugriffsrechte.

## 5. Ablaufvorschau und nachvollziehbare Ergebnisse

Wiederkehrende Tätigkeiten beginnen als deterministische Browserbefehle. Später können Abläufe Seiten lesen oder Formulare bedienen. Vor einem schreibenden Schritt werden Ziel und geplante Daten sichtbar. Die Ausführung protokolliert abgeschlossene Schritte und ermöglicht das Fortsetzen.

Eine KI bekommt geeignete Seiteninformationen über ausdrücklich definierte Schnittstellen. Der Passwort-Broker gibt ihr keine entschlüsselten Tresorinhalte. Wenn ein Passwort in das Formular einer Website eingefüllt wurde, kann die Website es grundsätzlich lesen; der Browser verspricht hier keine technisch unmögliche Trennung.

## 6. Ressourcenbudgets mit sichtbaren Auswirkungen

Arbeitsbereiche zeigen CPU-, Speicher- und Netzwerkaktivität. Der Nutzer kann Hintergrundarbeit begrenzen und gezielte Ausnahmen für Anrufe, Medien oder laufende Aufgaben erlauben. Der Browser erklärt, wenn eine Begrenzung eine Funktion pausiert.

Budgets beeinflussen Scheduler, Netzwerk und Prozesse. Ein schlafender Tab darf nicht als aktuell angezeigt werden, wenn seine Daten veraltet sind.

## 7. Bitwarden/Vaultwarden als fester Zugangsdatenbereich

Die Einrichtung bietet Bitwarden-Regionen und eigene HTTPS-Server. Der Browser zeigt vor dem Einfüllen das Konto und die tatsächliche Ziel-Domain. Mehrere Profile können unterschiedliche Konten verwenden.

Windows Hello, Passkeys, Organisationen und Synchronisierung benötigen eigene Kompatibilitätsprüfungen. Der Tresor wird nicht zum Ablageort für Browserprojekte umfunktioniert. Eine spätere eigene Synchronisierung kann selbst gehostet werden und bleibt ein separates System.

## Einordnung gegenüber anderen Browsern

Workspaces, vertikale Tabs, geteilte Ansichten, Container und Automationen existieren bereits in verschiedenen Browsern. Sie gehören als unterstützende Funktionen zum Konzept. Causalis soll durch die konsequente Verbindung von nachvollziehbarer Engine-Aktivität, alternativer Darstellung, passiven Seitenständen, Identitäten und Arbeitskontext auffallen. Wir behaupten keine weltweite Neuheit einzelner Bestandteile.

Primärquellen für diese Einordnung:

- https://resources.arc.net/hc/en-us/articles/19227964556183-Profiles-Separate-Work-Personal-Browsing
- https://vivaldi.com/features/workspaces/
- https://help.vivaldi.com/desktop/tabs/tab-tiling/
- https://support.mozilla.org/en-US/kb/containers
- https://brave.com/shields/
