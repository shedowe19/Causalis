# Bitwarden / Vaultwarden integration

Causalis 0.2 includes an original Windows bridge to a **separately installed, official native Bitwarden `bw.exe`**. It is a limited bridge for status, server configuration, encrypted synchronization, locking, and logout. It is not a complete vault client and does not implement autofill, passkeys, Windows Hello, login, unlock, or password retrieval. The custom browser engine cannot run the official Bitwarden browser extension.

The native Windows bridge was successfully compiled with MSVC on both `windows-latest` and `windows-2022` in the [2026-10-09 Windows run](https://github.com/shedowe19/Causalis/actions/runs/37914071885), using source commit `3b9d6291d9b6ac8b13e7cc48092f1c68d512def6`. Both runners passed all seven CTest targets, including the native UI smoke test and the portable policy tests for origin validation, command allowlisting, status parsing and account-field removal. The native smoke test does not invoke `bw.exe` or access a vault: real bridge operations, login state, server compatibility and Windows process/path handling still require runtime checks with a non-production test vault before treating the integration as working on an installed machine.

## What each operation means

| Operation | Behavior |
| --- | --- |
| Select CLI | Choose the exact path to a trusted native `bw.exe`; no shell, PATH lookup, download, installation, or executable signature assurance is performed by Causalis. Install and validate the CLI using Bitwarden's official instructions. |
| Status | Invoke `bw status --nointeraction`; display only vault state, validated server origin and last sync time. User email and user ID are dropped. Status is local CLI state, not a server reachability test. |
| Server configuration | First fetch fresh CLI status and require `unauthenticated`, then run `bw config server <validated-origin> --nointeraction`. Saving a server URL does not log in, establish an authenticated connection, or prove that the endpoint is Vaultwarden. |
| Sync | Invoke `bw sync --nointeraction` to ask the official client to pull encrypted data. Its success depends on prior authentication and the installed CLI version. Causalis does not pass an unlock session. |
| Lock | Invoke `bw lock --nointeraction`. This invalidates the CLI session, according to the official CLI behavior; it does not lock separate Bitwarden apps. |
| Logout | The narrow native API can invoke `bw logout --nointeraction`; it logs out only this workspace's CLI data. |

## Separate workspace data

Each of `Privat`, `Arbeit`, and `Homelab` has its own directory:

```text
%LOCALAPPDATA%\Causalis\workspaces\<workspace>\BitwardenCLI
```

The bridge sets `BITWARDENCLI_APPDATA_DIR` to that absolute directory on the child process only. It does not reuse the usual `%APPDATA%\Bitwarden CLI` vault. The host creates the workspace parent; the bridge creates its final `BitwardenCLI` subdirectory. It refuses alternate directories, UNC paths, mapped remote drives, unknown/nonexistent drive roots, optical drives, reparse-point components, relative executable paths, other executable names, and elevated browser processes. Only fixed, removable and RAM disk drive types qualify.

For actual account login, open PowerShell yourself, set the selected workspace directory, and interact directly with the official client:

```powershell
$env:BITWARDENCLI_APPDATA_DIR = Join-Path $env:LOCALAPPDATA 'Causalis\workspaces\Privat\BitwardenCLI'
& 'C:\Tools\Bitwarden\bw.exe' login
& 'C:\Tools\Bitwarden\bw.exe' lock
```

The executable path is an example: substitute the verified path you installed. The official CLI prompts for credentials. Do not put a master password or session key in a browser setting, command argument, file, or message. Do not set `BW_SESSION` in the browser environment. Causalis deliberately does not inherit any `BW_*` variables or session key, so an externally unlocked CLI session is not shared with its bridge; authenticated state normally appears as `locked`.

## Accepted servers

- Bitwarden US: exactly `https://vault.bitwarden.com`.
- Bitwarden EU: exactly `https://vault.bitwarden.eu`.
- Vaultwarden: an HTTPS origin such as `https://vault.example.net` or `https://vault.home:8443`.

Origin normalization lowercases DNS hostnames, removes a final root slash and default port 443, canonicalizes port numbers and hexadecimal IPv6 addresses. Credentials, URL queries, fragments, paths, percent escapes, backslashes, whitespace and non-ASCII hostnames are rejected. Use a DNS punycode hostname for internationalized domains. Bracketed hexadecimal IPv6 is supported; scoped addresses and IPv4-embedded IPv6 are not. A valid trusted HTTPS certificate is required by the official CLI. This initial bridge does not forward custom CA files or proxy environment settings and offers no certificate bypass.

## Process boundary

The browser uses `CreateProcessW` with an explicit executable, correctly quoted fixed arguments, a whitelisted child environment and `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. Only stdout/stderr pipes and a NUL stdin handle are inherited. Browser handles, passwords, API credentials, session keys, arbitrary `NODE_*` settings and proxy credentials are not intentionally forwarded. The CLI runs as the current normal user; this is **not a sandbox for an untrusted executable**. Select only a verified official CLI. Account state already present in the selected workspace directory remains accessible to that official client by design.

The selected executable and every ordinary directory component of its path and workspace path are opened before launch. The bridge checks attributes and the normalized final DOS path on those open handles, rejects reparse points, and holds the handles without write/delete sharing across process creation and the child's job lifetime. This prevents ordinary concurrent write/delete/rename/reparse replacement of the pinned objects while they are used. Child vault files remain writable so that the official CLI can update its own data. These source-reviewed checks do not promise protection against administrator/raw-volume modifications or a compromised CLI, and their interaction with Windows antivirus, indexing and directory-sharing behavior still needs runtime validation.

The browser's busy mutex serializes only operations in the current Causalis process. It does not lock `data.json` against a separately launched official CLI or another browser process. The logged-out status check and subsequent server-configuration command therefore are not one cross-process transaction: external CLI account changes between them may cause failure or a different observed account state. Do not run external CLI authentication/configuration concurrently with a server change. The bridge never automatically logs out an account to make a change succeed.

Pipe capture runs on the caller's worker thread and is limited to 64 KiB combined stdout/stderr and 20 seconds per command. The bridge uses a kill-on-close job to terminate a timed-out process and descendants. Server changes can use two commands and therefore take up to roughly 40 seconds. Concurrent bridge calls are rejected as busy instead of queued indefinitely. All stderr is discarded, action stdout is not shown, and status stdout is parsed before a restricted summary is returned. No raw CLI data is logged. Errors are generic; inspect official CLI errors yourself outside the browser when diagnosing account issues.

There is no local HTTP vault service, `bw serve`, arbitrary command execution, export, item listing, encrypted-data parser, custom cryptography, master password storage, or vault authentication built into Causalis. Full vault integration needs a separately reviewed protocol and origin-bound autofill design before decrypted secrets can cross into the custom browser.

## Official references checked on 2026-10-09

- [Bitwarden Password Manager CLI](https://bitwarden.com/help/cli/) — commands, `--nointeraction`, account isolation and login behavior.
- [Bitwarden data storage](https://bitwarden.com/help/data-storage/) — CLI directories and `BITWARDENCLI_APPDATA_DIR` override.
