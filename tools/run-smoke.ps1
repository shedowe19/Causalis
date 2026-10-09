param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$ProjectPath = ''
)
$ErrorActionPreference = 'Stop'
$arguments = @('--smoke-test')
if ($ProjectPath) {
    $quotedProjectPath = '"' + $ProjectPath + '"'
    $arguments = @($quotedProjectPath, '--smoke-test')
}
$process = Start-Process -FilePath $Executable -ArgumentList $arguments -PassThru
if (-not $process.WaitForExit(120000)) {
    & taskkill.exe /PID $process.Id /T /F | Out-Null
    throw 'Electron smoke test timed out.'
}
if ($process.ExitCode -ne 0) { throw "Electron smoke test failed with exit code $($process.ExitCode)." }
$reportFile = Join-Path $env:CAUSALIS_REPORT_DIR 'report.json'
if (-not (Test-Path -LiteralPath $reportFile -PathType Leaf)) { throw 'Smoke report was not created.' }
$report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
if ($report.success -ne $true) { throw 'Smoke report indicates a failure.' }
if ($report.tests.Count -lt 16) { throw 'Smoke report did not run the expected checks.' }
Write-Output "Actual Chromium smoke test: $($report.tests.Count) checks passed."
