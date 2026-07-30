[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$GeneratedDirectories = @(
    (Join-Path $RepositoryRoot "build"),
    (Join-Path $RepositoryRoot "out"),
    (Join-Path $RepositoryRoot "runtime")
)

$GeneratedDirectories += Get-ChildItem -LiteralPath $RepositoryRoot -Directory -Force |
    Where-Object { $_.Name -like "build-*" } |
    ForEach-Object { $_.FullName }

foreach ($Directory in $GeneratedDirectories | Select-Object -Unique) {
    if (Test-Path -LiteralPath $Directory) {
        Write-Host "Removing generated directory '$Directory'..."
        Remove-Item -LiteralPath $Directory -Recurse -Force
    }
}
