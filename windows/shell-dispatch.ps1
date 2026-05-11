param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('AddArchive', 'AddZip', 'AddZox', 'QuickAddZox', 'Extract', 'ExtractHere')]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$WinZOXPath,

    [Parameter(Mandatory = $true)]
    [string]$Target
)

$resolvedWinZOXPath = (Resolve-Path -LiteralPath $WinZOXPath).Path
$resolvedTarget = (Resolve-Path -LiteralPath $Target).Path

function Get-DefaultArchiveName {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $item = Get-Item -LiteralPath $Path
    if ($item.PSIsContainer) {
        return $item.Name
    }

    return [System.IO.Path]::GetFileNameWithoutExtension($Path)
}

function Get-ArchiveBasePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $item = Get-Item -LiteralPath $Path
    if ($item.PSIsContainer) {
        return $Path
    }

    $parent = Split-Path -Parent $Path
    $name = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    return (Join-Path $parent $name)
}

function Get-ArchiveOutputDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TargetPath
    )

    $targetItem = Get-Item -LiteralPath $TargetPath
    if ($targetItem.PSIsContainer) {
        Split-Path -Parent $TargetPath
    }
    else {
        Split-Path -Parent $TargetPath
    }
}

function Resolve-ArchiveOutputPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TargetPath,

        [Parameter(Mandatory = $true)]
        [string]$ArchiveName,

        [Parameter(Mandatory = $true)]
        [ValidateSet('zip', 'zox')]
        [string]$Format
    )

    $trimmedName = $ArchiveName.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmedName)) {
        throw 'Archive name cannot be empty.'
    }

    $outputDirectory = Get-ArchiveOutputDirectory -TargetPath $TargetPath
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($trimmedName)
    if ([string]::IsNullOrWhiteSpace($baseName)) {
        $baseName = $trimmedName
    }

    return (Join-Path $outputDirectory ($baseName + '.' + $Format))
}

function Show-AddArchiveDialog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TargetPath
    )

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    $defaultName = Get-DefaultArchiveName -Path $TargetPath
    $outputDirectory = Get-ArchiveOutputDirectory -TargetPath $TargetPath

    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'WinZOX - Add to archive'
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.ClientSize = New-Object System.Drawing.Size(460, 280)
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false

    $nameLabel = New-Object System.Windows.Forms.Label
    $nameLabel.Location = New-Object System.Drawing.Point(12, 18)
    $nameLabel.Size = New-Object System.Drawing.Size(100, 20)
    $nameLabel.Text = 'Archive name'
    $form.Controls.Add($nameLabel)

    $nameBox = New-Object System.Windows.Forms.TextBox
    $nameBox.Location = New-Object System.Drawing.Point(120, 15)
    $nameBox.Size = New-Object System.Drawing.Size(280, 23)
    $nameBox.Text = $defaultName
    $form.Controls.Add($nameBox)

    $formatLabel = New-Object System.Windows.Forms.Label
    $formatLabel.Location = New-Object System.Drawing.Point(12, 54)
    $formatLabel.Size = New-Object System.Drawing.Size(100, 20)
    $formatLabel.Text = 'Archive format'
    $form.Controls.Add($formatLabel)

    $formatBox = New-Object System.Windows.Forms.ComboBox
    $formatBox.Location = New-Object System.Drawing.Point(120, 51)
    $formatBox.Size = New-Object System.Drawing.Size(140, 23)
    $formatBox.DropDownStyle = 'DropDownList'
    [void]$formatBox.Items.Add('ZIP')
    [void]$formatBox.Items.Add('ZOX')
    $formatBox.SelectedIndex = 0
    $form.Controls.Add($formatBox)

    $compressionLabel = New-Object System.Windows.Forms.Label
    $compressionLabel.Location = New-Object System.Drawing.Point(12, 90)
    $compressionLabel.Size = New-Object System.Drawing.Size(100, 20)
    $compressionLabel.Text = 'Compression'
    $form.Controls.Add($compressionLabel)

    $compressionBox = New-Object System.Windows.Forms.ComboBox
    $compressionBox.Location = New-Object System.Drawing.Point(120, 87)
    $compressionBox.Size = New-Object System.Drawing.Size(140, 23)
    $compressionBox.DropDownStyle = 'DropDownList'
    $form.Controls.Add($compressionBox)

    $passwordLabel = New-Object System.Windows.Forms.Label
    $passwordLabel.Location = New-Object System.Drawing.Point(12, 126)
    $passwordLabel.Size = New-Object System.Drawing.Size(100, 20)
    $passwordLabel.Text = 'Password'
    $form.Controls.Add($passwordLabel)

    $passwordBox = New-Object System.Windows.Forms.TextBox
    $passwordBox.Location = New-Object System.Drawing.Point(120, 123)
    $passwordBox.Size = New-Object System.Drawing.Size(140, 23)
    $passwordBox.UseSystemPasswordChar = $true
    $form.Controls.Add($passwordBox)

    $encryptionLabel = New-Object System.Windows.Forms.Label
    $encryptionLabel.Location = New-Object System.Drawing.Point(12, 162)
    $encryptionLabel.Size = New-Object System.Drawing.Size(100, 20)
    $encryptionLabel.Text = 'Encryption'
    $form.Controls.Add($encryptionLabel)

    $encryptionBox = New-Object System.Windows.Forms.ComboBox
    $encryptionBox.Location = New-Object System.Drawing.Point(120, 159)
    $encryptionBox.Size = New-Object System.Drawing.Size(140, 23)
    $encryptionBox.DropDownStyle = 'DropDownList'
    [void]$encryptionBox.Items.Add('AES')
    [void]$encryptionBox.Items.Add('GORGON')
    $encryptionBox.SelectedIndex = 0
    $form.Controls.Add($encryptionBox)

    $splitLabel = New-Object System.Windows.Forms.Label
    $splitLabel.Location = New-Object System.Drawing.Point(12, 198)
    $splitLabel.Size = New-Object System.Drawing.Size(100, 20)
    $splitLabel.Text = 'Split size'
    $form.Controls.Add($splitLabel)

    $splitBox = New-Object System.Windows.Forms.TextBox
    $splitBox.Location = New-Object System.Drawing.Point(120, 195)
    $splitBox.Size = New-Object System.Drawing.Size(140, 23)
    $form.Controls.Add($splitBox)

    $pathLabel = New-Object System.Windows.Forms.Label
    $pathLabel.Location = New-Object System.Drawing.Point(12, 230)
    $pathLabel.Size = New-Object System.Drawing.Size(430, 32)
    $pathLabel.Text = "Save location: $outputDirectory"
    $form.Controls.Add($pathLabel)

    $okButton = New-Object System.Windows.Forms.Button
    $okButton.Location = New-Object System.Drawing.Point(284, 242)
    $okButton.Size = New-Object System.Drawing.Size(75, 25)
    $okButton.Text = 'OK'
    $okButton.DialogResult = [System.Windows.Forms.DialogResult]::OK
    $form.Controls.Add($okButton)

    $cancelButton = New-Object System.Windows.Forms.Button
    $cancelButton.Location = New-Object System.Drawing.Point(365, 242)
    $cancelButton.Size = New-Object System.Drawing.Size(75, 25)
    $cancelButton.Text = 'Cancel'
    $cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.Controls.Add($cancelButton)

    $form.AcceptButton = $okButton
    $form.CancelButton = $cancelButton

    $updateUiState = {
        $isZox = $formatBox.SelectedItem.ToString() -eq 'ZOX'

        $compressionBox.Items.Clear()
        if ($isZox) {
            [void]$compressionBox.Items.Add('ZSTD')
            [void]$compressionBox.Items.Add('ZLIB')
            [void]$compressionBox.Items.Add('STORE')
        } else {
            [void]$compressionBox.Items.Add('ZLIB')
            [void]$compressionBox.Items.Add('STORE')
        }
        $compressionBox.SelectedIndex = 0

        $passwordLabel.Enabled = $isZox
        $passwordBox.Enabled = $isZox
        $encryptionLabel.Enabled = $isZox
        $encryptionBox.Enabled = $isZox
        $splitLabel.Enabled = $isZox
        $splitBox.Enabled = $isZox

        if (-not $isZox) {
            $passwordBox.Text = ''
            $splitBox.Text = ''
            $encryptionBox.SelectedIndex = 0
        }
    }

    $formatBox.add_SelectedIndexChanged($updateUiState)
    & $updateUiState

    if ($form.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        return $null
    }

    $format = $formatBox.SelectedItem.ToString().ToLowerInvariant()
    return @{
        Format = $format
        OutputPath = Resolve-ArchiveOutputPath -TargetPath $TargetPath -ArchiveName $nameBox.Text -Format $format
        Compression = $compressionBox.SelectedItem.ToString().ToLowerInvariant()
        Password = $passwordBox.Text
        Encryption = $encryptionBox.SelectedItem.ToString().ToLowerInvariant()
        SplitSize = $splitBox.Text.Trim()
    }
}

switch ($Mode) {
    'AddArchive' {
        $selection = Show-AddArchiveDialog -TargetPath $resolvedTarget
        if ($null -ne $selection) {
            $arguments = @('add', $resolvedTarget)

            if ($selection.Format -eq 'zip') {
                $arguments += $selection.OutputPath
                $arguments += '--format'
                $arguments += 'zip'
                $arguments += '--algo'
                $arguments += $selection.Compression
            } else {
                $archiveBase = Join-Path (Split-Path -Parent $selection.OutputPath) ([System.IO.Path]::GetFileNameWithoutExtension($selection.OutputPath))
                $arguments += $archiveBase
                $arguments += '--format'
                $arguments += 'zox'
                $arguments += '--algo'
                $arguments += $selection.Compression

                if (-not [string]::IsNullOrWhiteSpace($selection.Password)) {
                    $arguments += '-p'
                    $arguments += $selection.Password
                    $arguments += '--encrypt'
                    $arguments += $selection.Encryption
                }

                if (-not [string]::IsNullOrWhiteSpace($selection.SplitSize)) {
                    $arguments += '-s'
                    $arguments += $selection.SplitSize
                }
            }

            & $resolvedWinZOXPath @arguments
        }
        break
    }

    'AddZip' {
        $selectedPath = Resolve-ArchiveOutputPath -TargetPath $resolvedTarget -ArchiveName (Get-DefaultArchiveName -Path $resolvedTarget) -Format 'zip'
        if ($null -ne $selectedPath) {
            & $resolvedWinZOXPath add $resolvedTarget $selectedPath --format zip
        }
        break
    }

    'AddZox' {
        $selectedPath = Resolve-ArchiveOutputPath -TargetPath $resolvedTarget -ArchiveName (Get-DefaultArchiveName -Path $resolvedTarget) -Format 'zox'
        if ($null -ne $selectedPath) {
            $archiveBase = Join-Path (Split-Path -Parent $selectedPath) ([System.IO.Path]::GetFileNameWithoutExtension($selectedPath))
            & $resolvedWinZOXPath add $resolvedTarget $archiveBase --format zox
        }
        break
    }

    'QuickAddZox' {
        $archiveBase = Get-ArchiveBasePath -Path $resolvedTarget
        & $resolvedWinZOXPath add $resolvedTarget $archiveBase --format zox
        break
    }

    'Extract' {
        $parent = Split-Path -Parent $resolvedTarget
        $name = [System.IO.Path]::GetFileNameWithoutExtension($resolvedTarget)
        $destination = Join-Path $parent $name
        & $resolvedWinZOXPath extract $resolvedTarget $destination
        break
    }

    'ExtractHere' {
        $destination = Split-Path -Parent $resolvedTarget
        & $resolvedWinZOXPath extract $resolvedTarget $destination
        break
    }
}
