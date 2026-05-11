param(
    [Parameter(Mandatory = $true)]
    [string]$WinZOXPath
)

$resolvedWinZOXPath = (Resolve-Path -LiteralPath $WinZOXPath).Path
$resolvedIconPath = Join-Path (Split-Path -LiteralPath $resolvedWinZOXPath -Parent) 'winzox-favicon.ico'
$iconValue = if (Test-Path -LiteralPath $resolvedIconPath) {
    $resolvedIconPath
} else {
    $resolvedWinZOXPath + ',0'
}
$classesRoot = 'Registry::HKEY_CURRENT_USER\Software\Classes'

function Set-RegistryValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -Path $Path -Force | Out-Null
    }

    Set-Item -Path $Path -Value $Value
}

function Set-RegistryNamedValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -Path $Path -Force | Out-Null
    }

    Set-ItemProperty -Path $Path -Name $Name -Value $Value
}

function New-CommandValue {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('shell-add', 'shell-quick-zox', 'shell-browse', 'shell-extract', 'shell-extract-here')]
        [string]$Mode,

        [switch]$Multiple
    )

    if ($Multiple) {
        return '"' + $resolvedWinZOXPath + '" ' + $Mode + ' %*'
    }

    return '"' + $resolvedWinZOXPath + '" ' + $Mode + ' "%1"'
}

$zipBoxArchiveKey = Join-Path $classesRoot 'WinZOX.Archive'
$zoxExtensionKey = Join-Path $classesRoot '.zox'

Set-RegistryValue -Path $zoxExtensionKey -Value 'WinZOX.Archive'
Set-RegistryValue -Path $zipBoxArchiveKey -Value 'WinZOX Archive'
Set-RegistryValue -Path (Join-Path $zipBoxArchiveKey 'DefaultIcon') -Value $iconValue
Set-RegistryValue -Path (Join-Path $zipBoxArchiveKey 'shell\open') -Value 'Open with WinZOX'
Set-RegistryNamedValue -Path (Join-Path $zipBoxArchiveKey 'shell\open') -Name 'Icon' -Value $iconValue
Set-RegistryValue -Path (Join-Path $zipBoxArchiveKey 'shell\open\command') -Value (New-CommandValue -Mode 'shell-browse')
Set-RegistryValue -Path (Join-Path $zipBoxArchiveKey 'shell\extract') -Value 'Extract with WinZOX'
Set-RegistryNamedValue -Path (Join-Path $zipBoxArchiveKey 'shell\extract') -Name 'Icon' -Value $iconValue
Set-RegistryValue -Path (Join-Path $zipBoxArchiveKey 'shell\extract\command') -Value (New-CommandValue -Mode 'shell-extract')
Set-RegistryValue -Path (Join-Path $zipBoxArchiveKey 'shell\extract_here') -Value 'Extract Here with WinZOX'
Set-RegistryNamedValue -Path (Join-Path $zipBoxArchiveKey 'shell\extract_here') -Name 'Icon' -Value $iconValue
Set-RegistryValue -Path (Join-Path $zipBoxArchiveKey 'shell\extract_here\command') -Value (New-CommandValue -Mode 'shell-extract-here')

$fileShellKey = Join-Path $classesRoot '*\shell\WinZOXAddToZox'
$directoryShellKey = Join-Path $classesRoot 'Directory\shell\WinZOXAddToZox'
$fileArchiveMenuKey = Join-Path $classesRoot '*\shell\WinZOXAddToArchive'
$directoryArchiveMenuKey = Join-Path $classesRoot 'Directory\shell\WinZOXAddToArchive'

Set-RegistryValue -Path $fileShellKey -Value 'Add to .zox'
Set-RegistryNamedValue -Path $fileShellKey -Name 'Icon' -Value $iconValue
Set-RegistryValue -Path (Join-Path $fileShellKey 'command') -Value (New-CommandValue -Mode 'shell-quick-zox' -Multiple)
Set-RegistryValue -Path $directoryShellKey -Value 'Add to .zox'
Set-RegistryNamedValue -Path $directoryShellKey -Name 'Icon' -Value $iconValue
Set-RegistryValue -Path (Join-Path $directoryShellKey 'command') -Value (New-CommandValue -Mode 'shell-quick-zox')

foreach ($archiveMenuKey in @($fileArchiveMenuKey, $directoryArchiveMenuKey)) {
    Set-RegistryValue -Path $archiveMenuKey -Value 'Add to archive'
    Set-RegistryNamedValue -Path $archiveMenuKey -Name 'MUIVerb' -Value 'Add to archive'
    Set-RegistryNamedValue -Path $archiveMenuKey -Name 'Icon' -Value $iconValue

    if (Test-Path -LiteralPath (Join-Path $archiveMenuKey 'shell')) {
        Remove-Item -LiteralPath (Join-Path $archiveMenuKey 'shell') -Recurse -Force
    }

    if ((Get-Item -LiteralPath $archiveMenuKey).Property -contains 'SubCommands') {
        Remove-ItemProperty -Path $archiveMenuKey -Name 'SubCommands'
    }

    $commandValue = if ($archiveMenuKey -like '*\*\shell\*') {
        New-CommandValue -Mode 'shell-add' -Multiple
    } else {
        New-CommandValue -Mode 'shell-add'
    }

    Set-RegistryValue -Path (Join-Path $archiveMenuKey 'command') -Value $commandValue
}

Write-Host 'WinZOX shell integration installed for the current user.'
