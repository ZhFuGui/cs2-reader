param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$metadata = Get-Content -LiteralPath $InputPath -Raw | ConvertFrom-Json
$number = [uint32]$metadata.build_number
if ($number -eq 0) { throw 'Offset metadata must contain a nonzero build_number.' }
$timestamp = [string]$metadata.timestamp
if ($timestamp -notmatch '^[0-9T:.+Z-]+$') { throw 'Invalid offset generation timestamp.' }
$source = "#pragma once`n#include <cstdint>`nnamespace cs2::game::generated {`ninline constexpr std::uint32_t build_number = $($number)U;`ninline constexpr char timestamp[] = `"$timestamp`";`n}`n"
$parent = [System.IO.Path]::GetDirectoryName($OutputPath)
[System.IO.Directory]::CreateDirectory($parent) | Out-Null
if (!(Test-Path -LiteralPath $OutputPath) -or (Get-Content -LiteralPath $OutputPath -Raw) -cne $source) {
    [System.IO.File]::WriteAllText($OutputPath, $source, (New-Object System.Text.UTF8Encoding($false)))
}
