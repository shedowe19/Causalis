# Chromium-Architektur

Die Browserbasis ist Electron 44.7.0 mit Chromium und V8. Ein vertrauenswürdiges BrowserWindow enthält ausschließlich die lokale Oberfläche unter `causalis://ui/index.html`. Websites erhalten jeweils eine WebContentsView mit ihrer Workspace-Session. Es werden keine Browser-APIs über einen Website-Preload freigegeben.

| Grenze | Umsetzung |
| --- | --- |
| Browseroberfläche | Eigene nicht persistente Session, CSP, erlaubte lokale Ressourcenliste, keine Navigation oder Popups |
| Websites | Chromium-Sandbox, Kontextisolation, Websicherheit, kein Node/Preload/WebView-Tag |
| Website-Navigation | HTTP/HTTPS, kontrollierte Tab-Popups; lokale Dateien und privilegierte UI-Protokolle gesperrt |
| Arbeitsbereiche | Drei getrennte persistente Sessions; privater Bereich mit eigener In-Memory-Session |
| Tresor | Offizielle CLI als begrenzter Kindprozess, expliziter Pfad, keine Shell, feste Befehle und Ausgabelimits |
| Tresor-IPC | Nur tatsächliches UI-WebContents, tatsächlicher Hauptframe und exakte UI-URL |
| Zugangsdaten | Hauptprozess-RAM, Auswahl mit Ablaufzeit und erneuter URI-Prüfung; kein UI-Geheimnisabruf |
| Autofill | Aktiver Tab, Navigationsepoche und HTTPS-Origin erneut geprüft; isolierte Hauptframe-Ausführung prüft sichtbare Felder und Formularziele |

Die CLI verwaltet die verschlüsselten Kontodaten selbst. Causalis erhält beim Entsperren einen Sitzungsschlüssel und hält ihn im Hauptprozess-RAM. Der Schlüssel wird ausschließlich in der Umgebung des einzelnen Kindprozesses weitergegeben. Eine Sperre invalidiert laufende Abrufe und Auswahlberechtigungen sofort; nachfolgende CLI-Aufräumarbeiten geben sie nicht wieder frei.

Die WebContentsView erhält Zugangsdaten erst beim ausdrücklichen Einfüllen. Ab diesem Zeitpunkt kann die ausgewählte Website ihre eigenen Formularwerte lesen. Seitenwechsel, fremde Frames, versteckte/neue Passwortfelder, mehrere mehrdeutige Passwortfelder und fremde Form- oder Submit-Ziele blockieren die Operation. Es erfolgt kein automatisches Absenden.

Ein Chromium-Passwortmanager wird nicht eingebunden. Das ist eine Entscheidung über die eingebetteten Komponenten, kein Versprechen über unbestätigte Chrome-Kommandozeilenflags. Der Laufzeittest prüft zusätzlich, dass das frische Profil keine `Login Data`-Passwortdatenbank erzeugt.

Das Windows-Paket deaktiviert die Electron-Fuses RunAsNode, NODE_OPTIONS, Node-Inspektorargumente und zusätzliche Dateiprotokollprivilegien. Die Cookieverschlüsselung bleibt aktiviert. Es gibt noch keinen signierten Updatekanal oder vollständigen Berechtigungsdialog.

Primärquellen: [Electron Security](https://www.electronjs.org/docs/latest/tutorial/security), [WebContentsView](https://www.electronjs.org/docs/latest/api/web-contents-view), [Electron-Erweiterungsgrenzen](https://www.electronjs.org/docs/latest/api/extensions), [Fuses](https://www.electronjs.org/docs/latest/tutorial/fuses), [Bitwarden CLI](https://bitwarden.com/help/cli/).
