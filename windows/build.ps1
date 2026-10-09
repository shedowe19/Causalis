param([string]$OutputDirectory = "")

$ErrorActionPreference = "Stop"
$project = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $project "build-windows"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "cl.exe fehlt. Starte eine x64 Native Tools Command Prompt für Visual Studio 2022 und führe dieses Skript dort mit PowerShell aus."
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$arguments = @(
    "/nologo", "/std:c++20", "/EHsc", "/W4", "/utf-8", "/O2", "/MT",
    "/DUNICODE", "/D_UNICODE", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN",
    "/I$project\include",
    "$project\src\engine.cpp",
    "$project\src\document.cpp",
    "$project\src\script.cpp",
    "$project\src\page.cpp",
    "$project\src\checkpoint.cpp",
    "$project\src\vault_policy.cpp",
    "$project\windows\browser_support.cpp",
    "$project\windows\vault_bridge.cpp",
    "$project\windows\main.cpp",
    "/Fe:$OutputDirectory\causalis.exe",
    "/link", "/SUBSYSTEM:WINDOWS", "user32.lib", "gdi32.lib", "comdlg32.lib",
    "comctl32.lib", "winhttp.lib", "shell32.lib", "advapi32.lib"
)

Push-Location $OutputDirectory
try {
    & cl.exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Der Windows-Build ist fehlgeschlagen (Exitcode $LASTEXITCODE)."
    }
} finally {
    Pop-Location
}
Write-Output "Erstellt: $OutputDirectory\causalis.exe"
Write-Output "Eigene experimentelle Engine: Dokumente, Projekte, HTTPS-Versuch und Bitwarden-CLI-Verwaltung. Noch keine vollständige Web-Kompatibilität, Sandbox oder Passwort-Autofill."
