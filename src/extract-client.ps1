param(
    [Parameter(Mandatory=$true,Position=0)][string]$ArchivePath,
    [Parameter(Mandatory=$true,Position=1)][string]$DestinationPath
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Archive\Microsoft.PowerShell.Archive.psd1') -Force
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $DestinationPath
if (-not (Test-Path -LiteralPath (Join-Path $DestinationPath 'bw.exe') -PathType Leaf)) {
    throw 'Official Bitwarden archive did not contain bw.exe.'
}
