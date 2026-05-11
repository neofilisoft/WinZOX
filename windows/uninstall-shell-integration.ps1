$classesRoot = 'Registry::HKEY_CURRENT_USER\Software\Classes'

$keys = @(
    (Join-Path $classesRoot '.zox'),
    (Join-Path $classesRoot 'WinZOX.Archive'),
    (Join-Path $classesRoot '*\shell\WinZOXAddToZox'),
    (Join-Path $classesRoot 'Directory\shell\WinZOXAddToZox'),
    (Join-Path $classesRoot '*\shell\WinZOXAddToArchive'),
    (Join-Path $classesRoot 'Directory\shell\WinZOXAddToArchive')
)

foreach ($key in $keys) {
    if (Test-Path -LiteralPath $key) {
        Remove-Item -LiteralPath $key -Recurse -Force
    }
}

Write-Host 'WinZOX shell integration removed for the current user.'
