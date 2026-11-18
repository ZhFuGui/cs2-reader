param(
    [Parameter(Mandatory=$true)][string]$ReaderRoot,
    [string]$OutputDirectory,
    [switch]$UpdateMarkdown
)
$ErrorActionPreference = 'Stop'

function Write-GeneratedFile([string]$Path, [string]$Content) {
    if ([IO.File]::Exists($Path) -and [IO.File]::ReadAllText($Path) -ceq $Content) {
        return
    }
    [IO.File]::WriteAllText($Path, $Content, [Text.UTF8Encoding]::new($false))
}

$catalogDirectory = Join-Path (Resolve-Path -LiteralPath $ReaderRoot).Path 'protocol\reader'
$catalog = Get-Content -LiteralPath (Join-Path $catalogDirectory 'diagnostic-codes.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$markdownPath = Join-Path $catalogDirectory 'diagnostic-codes.md'
$markdown = [IO.File]::ReadAllText($markdownPath).Replace("`r`n", "`n")
$updatedMarkdown = $markdown
$headers = @{}
$groups = @(
    @{ Key = 'reasons'; Prefix = 'R'; Namespace = 'cs2::model'; Enum = 'ReasonCode'; File = 'reason_codes.generated.hpp'; Heading = '| 编号 | C++ 名称 | 含义 | 使用位置或行为 |'; Zero = '    None = 0,' },
    @{ Key = 'events'; Prefix = 'E'; Namespace = 'cs2::diagnostics'; Enum = 'EventId'; File = 'event_ids.generated.hpp'; Heading = '| 编号 | C++ 名称 | 事件 | 主要字段 |' }
)

foreach ($group in $groups) {
    $key = $group.Key
    $entries = $catalog.$key
    if ($entries -isnot [array] -or $entries.Count -eq 0) {
        throw "Diagnostic catalog '$key' must be a nonempty array."
    }
    $ids = [Collections.Generic.HashSet[uint32]]::new()
    $symbols = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in $entries) {
        $id = $entry.id
        if (($id -isnot [int] -and $id -isnot [long]) -or $id -lt 1 -or $id -gt [uint32]::MaxValue) {
            throw "Diagnostic catalog '$key' has an invalid ID '$id'."
        }
        if (-not $ids.Add([uint32]$id)) {
            throw "Diagnostic catalog '$key' repeats ID $id."
        }
        $symbol = $entry.symbol
        if ($symbol -isnot [string] -or $symbol -cnotmatch '\A[A-Z][A-Za-z0-9]*\z' -or $symbol -ceq 'None' -or -not $symbols.Add($symbol)) {
            throw "Diagnostic catalog '$key' has an invalid or repeated symbol '$symbol' at ID $id."
        }
        foreach ($field in @('meaning', 'usage')) {
            $value = $entry.$field
            if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value) -or $value -match '[\r\n|]') {
                throw "Diagnostic catalog '$key' has invalid '$field' text at ID $id."
            }
        }
    }

    $entries = $entries | Sort-Object -Property id
    $rows = foreach ($entry in $entries) {
        $usage = $entry.usage
        if ($entry.PSObject.Properties['availability']) {
            $availability = $entry.availability
            if ($key -ne 'reasons' -or ($availability -isnot [int] -and $availability -isnot [long]) -or $availability -lt 1 -or $availability -gt 4) {
                throw "Diagnostic catalog '$key' has invalid availability at ID $($entry.id)."
            }
            $usage = '`{0}/{1}`。{2}' -f $availability, $entry.id, $usage
        }
        '| `{0}{1:D3}` | `{2}` | {3} | {4} |' -f $group.Prefix, $entry.id, $entry.symbol, $entry.meaning, $usage
    }
    $begin = "<!-- BEGIN GENERATED $key -->"
    $end = "<!-- END GENERATED $key -->"
    $pattern = '(?ms)^' + [regex]::Escape($begin) + '\n.*?^' + [regex]::Escape($end) + '$'
    $blocks = [regex]::Matches($updatedMarkdown, $pattern)
    if ($blocks.Count -ne 1) {
        throw "Diagnostic reference must contain exactly one '$key' generated block."
    }
    $block = $blocks[0]
    $replacement = (@($begin, '', $group.Heading, '|---|---|---|---|') + $rows + @('', $end)) -join "`n"
    $updatedMarkdown = $updatedMarkdown.Remove($block.Index, $block.Length).Insert($block.Index, $replacement)

    $lines = @(
        '// Generated from protocol/reader/diagnostic-codes.json.'
        '#pragma once'
        '#include <cstdint>'
        ''
        "namespace $($group.Namespace) {"
        "enum class $($group.Enum) : std::uint32_t {"
    )
    if ($group.Zero) { $lines += $group.Zero }
    $lines += foreach ($entry in $entries) { "    $($entry.symbol) = $($entry.id)," }
    $lines += @('};', "} // namespace $($group.Namespace)", '')
    $headers[$group.File] = $lines -join "`n"
}

if ($markdown -cne $updatedMarkdown) {
    if (-not $UpdateMarkdown) {
        throw 'Diagnostic reference is out of date. Run build/generate_diagnostic_codes.ps1 -ReaderRoot . -UpdateMarkdown.'
    }
    Write-GeneratedFile $markdownPath $updatedMarkdown
}

if ($OutputDirectory) {
    $directory = [IO.Directory]::CreateDirectory($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory))
    foreach ($header in $headers.GetEnumerator()) {
        Write-GeneratedFile (Join-Path $directory.FullName $header.Key) $header.Value
    }
}
