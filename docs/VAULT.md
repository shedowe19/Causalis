# Bitwarden und Vaultwarden

Causalis bietet eine eigene integrierte Tresoroberfläche über den offiziellen nativen Bitwarden-CLI. Der Client erledigt Anmeldung, Schlüsselableitung, Verschlüsselung und Synchronisierung. Causalis implementiert keine eigene Tresorkryptografie und speichert keine eigenen Klartext-Passwörter.

Der automatische Download ist auf `bw-oss-windows-2026.9.1.zip` festgelegt. SHA-256: `c39d346239d926ad7955d1ee7acddf557ff216f3186b5383f40acc402f1adefe`. Das Archiv wird vor dem Entpacken geprüft, anschließend wird der Hash der installierten EXE gespeichert und vor jedem CLI-Aufruf überprüft. Eine selbst gewählte EXE wird auf Form und CLI-Version geprüft; das beweist nicht ihre offizielle Herkunft. Der geprüfte Download ist deshalb der einfachere Weg.

`BITWARDENCLI_APPDATA_DIR` zeigt je Arbeitsbereich auf einen getrennten Unterordner unter Causalis' `userData/vaults/`. Auch das private Browserprofil besitzt einen verschlüsselten CLI-Cache auf dem Gerät. Die üblichen Profile anderer Bitwarden-Apps werden nicht übernommen. Desktop-App und Causalis teilen keinen automatischen Entsperrstatus.

Masterpasswort und API-Geheimnis gelangen über die Umgebung des einzelnen Kindprozesses an den offiziellen Client; Login-Datensätze zum Speichern werden Base64-kodiert über stdin übergeben. Base64 ist keine Verschlüsselung. Es werden weder Passwortargumente noch Sitzungsschlüssel in Prozessargumenten verwendet. CLI-Ausgaben werden begrenzt und vor UI-Rückgabe auf freigegebene Metadaten reduziert; rohe Fehlermeldungen werden verworfen.

Die Umgebung ist begrenzt und übernimmt keine fremden BW_-, NODE_- oder ELECTRON_-Einstellungen. Der offizielle Client läuft als derselbe normale Windows-Nutzer. Diese Prozessgrenze ist keine Sandbox für eine manipulierte EXE oder Schutz vor einem kompromittierten Benutzerkonto.

Einträge werden nur bei exakter HTTPS-Origin angeboten. Eintrag-ID, Origin und Auswahl sind kurzzeitig autorisiert; beim Abruf wird der Eintrag erneut vom CLI geholt und seine URI erneut geprüft. Änderungen, Sperren und Profilwechsel invalidieren die Freigabe. Einträge mit erneuter Masterpasswortabfrage oder deaktivierter URI-Zuordnung werden nicht ausgefüllt.

Automatisches Entsperren mit Windows Hello, Passkeys, Ausfüllen fremder Frames und allgemeine Browsererweiterungen fehlen. CLI-Login unterstützt Authenticator, E-Mail und YubiKey OTP; API-Schlüssel plus Masterpasswort stehen als Alternative bereit. Serverkompatibilität und reale Zwei-Faktor-Flows müssen mit einem Testkonto praktisch verifiziert werden.

Quellen: [offizielle CLI-Dokumentation](https://bitwarden.com/help/cli/), [festgelegter CLI-Quellstand](https://github.com/bitwarden/clients/tree/8246ae9c9a484a0a69f8b27203034555fb872523/apps/cli), [Vaultwarden](https://github.com/dani-garcia/vaultwarden).
