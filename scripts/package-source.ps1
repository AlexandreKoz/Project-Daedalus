[CmdletBinding()]
param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$RepositoryParent = Split-Path -Parent $RepositoryRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $RepositoryParent "Project-Daedalus-Campaign-B-audit-closure-source.zip"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$CanonicalRoot = "Project-Daedalus"
$FixedTimestamp = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
$RepositoryPrefix = $RepositoryRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if ($OutputPath.StartsWith($RepositoryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $OutputPath.Equals($RepositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output archive must be outside the repository root."
}

$ForbiddenDirectoryNames = @(
    ".git", ".vs", ".vscode", ".idea", "build", "out", "bin", "obj", "Debug", "Release",
    "RelWithDebInfo", "MinSizeRel", "x64", "CMakeFiles", "Testing", "__pycache__", "runtime"
)
$ForbiddenFilePatterns = @(
    "CMakeCache.txt", "cmake_install.cmake", "CTestTestfile.cmake", "compile_commands.json",
    "*.sln", "*.vcxproj", "*.vcxproj.filters", "*.vcxproj.user", "*.pdb", "*.ilk", "*.obj",
    "*.lib", "*.exp", "*.exe", "*.dll", "*.cso", "*.dxil", "*.cache", "*.log", "*.zip",
    "*.tar", "*.tar.gz", "*.7z", "*.pyc", "*.pyo"
)

function Test-ForbiddenRelativePath([string]$RelativePath, [bool]$IsDirectory) {
    $Parts = $RelativePath.Replace('\', '/').Split('/', [System.StringSplitOptions]::RemoveEmptyEntries)
    foreach ($Part in $Parts) {
        if ($ForbiddenDirectoryNames -contains $Part -or $Part -like "build-*") { return $true }
    }
    if (-not $IsDirectory) {
        $Leaf = $Parts[-1]
        foreach ($Pattern in $ForbiddenFilePatterns) {
            if ($Leaf -like $Pattern) { return $true }
        }
    }
    return $false
}

$Violations = [System.Collections.Generic.List[string]]::new()
$Files = [System.Collections.Generic.List[object]]::new()
Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -Force | ForEach-Object {
    $Relative = [System.IO.Path]::GetRelativePath($RepositoryRoot, $_.FullName).Replace('\', '/')
    if (($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        $Violations.Add("reparse-point:$Relative")
    }
    elseif (Test-ForbiddenRelativePath $Relative $_.PSIsContainer) {
        $Violations.Add($Relative)
    }
    elseif (-not $_.PSIsContainer) {
        $Files.Add([PSCustomObject]@{ Relative = $Relative; FullName = $_.FullName })
    }
}
if ($Violations.Count -gt 0) {
    throw "Source archive rejected because generated or prohibited artefacts exist:`n$($Violations -join "`n")"
}

if ($Files.Count -eq 0) { throw "Source archive rejected because no source files were found." }
$FilesByRelative = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
foreach ($File in $Files) { $FilesByRelative.Add([string]$File.Relative, $File) }
$RelativePaths = [string[]]$FilesByRelative.Keys
[Array]::Sort($RelativePaths, [StringComparer]::Ordinal)
$Files = @($RelativePaths | ForEach-Object { $FilesByRelative[$_] })

$OutputDirectory = Split-Path -Parent $OutputPath
if ($OutputDirectory) { New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null }
if (Test-Path -LiteralPath $OutputPath) { Remove-Item -LiteralPath $OutputPath -Force }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Stream = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
try {
    $Archive = [System.IO.Compression.ZipArchive]::new($Stream, [System.IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        foreach ($File in $Files) {
            $EntryName = "$CanonicalRoot/$($File.Relative)"
            $Entry = $Archive.CreateEntry($EntryName, [System.IO.Compression.CompressionLevel]::Optimal)
            $Entry.LastWriteTime = $FixedTimestamp
            $Input = [System.IO.File]::OpenRead($File.FullName)
            try {
                $Output = $Entry.Open()
                try { $Input.CopyTo($Output) }
                finally { $Output.Dispose() }
            }
            finally { $Input.Dispose() }
        }
    }
    finally { $Archive.Dispose() }
}
finally { $Stream.Dispose() }

$ReadArchive = [System.IO.Compression.ZipFile]::OpenRead($OutputPath)
try {
    $Names = [string[]]@($ReadArchive.Entries | ForEach-Object { $_.FullName })
    $SortedNames = [string[]]$Names.Clone()
    [Array]::Sort($SortedNames, [StringComparer]::Ordinal)
    if ([string]::Join("`n", $Names) -cne [string]::Join("`n", $SortedNames)) {
        throw "Archive inspection found non-lexical entry order."
    }
    $UniqueNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($Entry in $ReadArchive.Entries) {
        if (-not $UniqueNames.Add($Entry.FullName)) {
            throw "Archive inspection found a duplicate entry: $($Entry.FullName)"
        }
        if (-not $Entry.FullName.StartsWith("$CanonicalRoot/", [StringComparison]::Ordinal)) {
            throw "Archive inspection found an entry outside the canonical root: $($Entry.FullName)"
        }
        if ($Entry.LastWriteTime -ne $FixedTimestamp) {
            throw "Archive inspection found a non-canonical timestamp: $($Entry.FullName)"
        }
        $Relative = $Entry.FullName.Substring($CanonicalRoot.Length + 1)
        if (Test-ForbiddenRelativePath $Relative $false) {
            throw "Archive inspection found a prohibited entry: $($Entry.FullName)"
        }
        $EntryStream = $Entry.Open()
        try {
            $Buffer = [byte[]]::new(81920)
            while ($EntryStream.Read($Buffer, 0, $Buffer.Length) -gt 0) { }
        }
        finally { $EntryStream.Dispose() }
    }
}
finally { $ReadArchive.Dispose() }

$Hash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Archive: $OutputPath"
Write-Host "Entries: $($Files.Count)"
Write-Host "Fixed timestamp: $($FixedTimestamp.ToString('o'))"
Write-Host "Archive SHA-256: $Hash"
