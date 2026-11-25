param([Parameter(Mandatory=$true)][string]$ReaderRoot)
$ErrorActionPreference = 'Stop'
function Get-Digest([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([System.BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}
$manifestPath = Join-Path $ReaderRoot 'protocol\reader\v2\schema-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$descriptorPath = Join-Path $ReaderRoot ('protocol\reader\v2\' + $manifest.descriptor)
if ((Get-Digest $descriptorPath) -ne $manifest.schema_sha256) {
    throw 'Frozen protocol descriptor mismatch; run build/freeze_protocol.py with grpcio-tools.'
}
foreach ($source in $manifest.sources.PSObject.Properties) {
    $sourcePath = Join-Path (Join-Path $ReaderRoot 'protocol') $source.Name
    if ((Get-Digest $sourcePath) -ne $source.Value) {
        throw ('Protocol source changed without freezing: ' + $source.Name)
    }
}
$headerPath = Join-Path $ReaderRoot 'sdk\include\cs2_reader\v2\schema.hpp'
$header = Get-Content -LiteralPath $headerPath -Raw
if (-not $header.Contains($manifest.schema_sha256)) {
    throw 'Protocol header hash does not match frozen descriptor.'
}
if (-not $header.Contains("protocol_version = $($manifest.protocol_version);")) {
    throw 'Protocol header version does not match the manifest.'
}
