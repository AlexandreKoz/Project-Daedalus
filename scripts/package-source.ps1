[CmdletBinding()]
param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepositoryParent = Split-Path -Parent $RepositoryRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $RepositoryParent "Project-Daedalus-Campaign-B-source.zip"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

$ForbiddenDirectoryNames = @(
    ".vs", ".vscode", ".idea", "build", "out", "bin", "obj", "Debug", "Release",
    "RelWithDebInfo", "MinSizeRel", "x64", "CMakeFiles", "Testing", "__pycache__", "runtime"
)
$ForbiddenFilePatterns = @(
    "CMakeCache.txt", "cmake_install.cmake", "CTestTestfile.cmake", "compile_commands.json",
    "*.sln", "*.vcxproj", "*.vcxproj.filters", "*.vcxproj.user", "*.pdb", "*.ilk", "*.obj",
    "*.lib", "*.exp", "*.exe", "*.dll", "*.cso", "*.dxil", "*.cache", "*.log", "*.zip"
)

$Violations = [System.Collections.Generic.List[string]]::new()
Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -Force | ForEach-Object {
    $Relative = $_.FullName.Substring($RepositoryRoot.Length).TrimStart([char[]]"\/")
    $NormalizedRelative = $Relative.Replace('\', '/')
    if ($NormalizedRelative -match '(^|/)\.git($|/)') {
        return
    }

    if ($_.PSIsContainer) {
        if ($ForbiddenDirectoryNames -contains $_.Name -or $_.Name -like "build-*") {
            $Violations.Add($Relative)
        }
    }
    else {
        foreach ($Pattern in $ForbiddenFilePatterns) {
            if ($_.Name -like $Pattern) {
                $Violations.Add($Relative)
                break
            }
        }
    }
}

if ($Violations.Count -gt 0) {
    throw "Source archive rejected because generated or prohibited artefacts exist:`n$($Violations -join "`n")"
}

$TemporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("DaedalusPackage-" + [Guid]::NewGuid().ToString("N"))
$StagingRepository = Join-Path $TemporaryRoot "Project-Daedalus"
New-Item -ItemType Directory -Path $StagingRepository -Force | Out-Null

try {
    Get-ChildItem -LiteralPath $RepositoryRoot -Force | Where-Object { $_.Name -ne ".git" } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $StagingRepository -Recurse -Force
    }

    Get-ChildItem -LiteralPath $StagingRepository -Recurse -Force |
        Where-Object { $_.Name -eq ".git" } |
        Sort-Object FullName -Descending |
        ForEach-Object {
            if ($_.PSIsContainer) {
                Remove-Item -LiteralPath $_.FullName -Recurse -Force
            }
            else {
                Remove-Item -LiteralPath $_.FullName -Force
            }
        }

    if (Test-Path -LiteralPath $OutputPath) {
        Remove-Item -LiteralPath $OutputPath -Force
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null

    Write-Host "Creating clean source archive '$OutputPath'..."
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $TemporaryRoot,
        $OutputPath,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false)

    $Archive = [System.IO.Compression.ZipFile]::OpenRead($OutputPath)
    try {
        foreach ($Entry in $Archive.Entries) {
            $EntryName = $Entry.FullName.Replace('\', '/')
            $Leaf = Split-Path -Leaf $EntryName
            if ($EntryName -match '(^|/)(build[^/]*|out|bin|obj|Debug|Release|RelWithDebInfo|MinSizeRel|x64|CMakeFiles|Testing|runtime|\.git)(/|$)' -or
                $Leaf -match '\.(pdb|ilk|obj|lib|exp|exe|dll|cso|dxil|cache|log|zip)$') {
                throw "Archive inspection found a prohibited entry: $EntryName"
            }
        }
    }
    finally {
        $Archive.Dispose()
    }

    $Hash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "Archive SHA-256: $Hash"
}
finally {
    if (Test-Path -LiteralPath $TemporaryRoot) {
        Remove-Item -LiteralPath $TemporaryRoot -Recurse -Force
    }
}
