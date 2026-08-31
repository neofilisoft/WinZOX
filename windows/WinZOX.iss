#define MyAppName "WinZOX"
#define MyAppVersion "3.1.1"
#define MyAppPublisher "Neofilisoft"
#define MyAppExeName "zox.exe"
#define BuildDir "..\\build-ninja5"

[Setup]
AppId={{7F4A3C5B-63E7-4E18-8C38-5B1F9EAA4F4D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://neofilisoft.com
DefaultDirName={autopf}\{#MyAppName}
OutputDir=.
OutputBaseFilename=WinZOXSetup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
ChangesAssociations=yes
DisableWelcomePage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\favicon.ico
SetupIconFile=..\favicon.ico
VersionInfoCompany={#MyAppPublisher}
VersionInfoProductName={#MyAppName}
VersionInfoDescription={#MyAppName} Installer

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#BuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\zox.exe.manifest"; DestDir: "{app}"; DestName: "zox.exe.manifest"; Flags: ignoreversion
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\favicon.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\windows\install-shell-integration.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "..\windows\uninstall-shell-integration.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion

[Registry]
Root: HKLM; Subkey: "Software\Classes\.zox"; ValueType: string; ValueName: ""; ValueData: "WinZOX.Archive"; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive"; ValueType: string; ValueName: ""; ValueData: "WinZOX Archive"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\winzox-favicon.ico"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell"; ValueType: string; ValueName: ""; ValueData: "open"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\open"; ValueType: string; ValueName: ""; ValueData: "Open with WinZOX"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\open"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\open"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-browse ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_files"; ValueType: string; ValueName: ""; ValueData: "Extract Files..."; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_files"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_files"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_files\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract-files ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_here"; ValueType: string; ValueName: ""; ValueData: "Extract Here"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_here"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_here"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_here\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract-here ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_to"; ValueType: string; ValueName: ""; ValueData: "Extract to Archive Folder"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_to"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_to"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\WinZOX.Archive\shell\extract_to\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXOpen"; ValueType: string; ValueName: ""; ValueData: "Open with WinZOX"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXOpen"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXOpen"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXOpen\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-browse ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractFiles"; ValueType: string; ValueName: ""; ValueData: "Extract Files..."; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractFiles"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractFiles"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractFiles\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract-files ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractHere"; ValueType: string; ValueName: ""; ValueData: "Extract Here"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractHere"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractHere"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractHere\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract-here ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractTo"; ValueType: string; ValueName: ""; ValueData: "Extract to Archive Folder"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractTo"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractTo"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.zip\shell\WinZOXExtractTo\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXOpen"; ValueType: string; ValueName: ""; ValueData: "Open with WinZOX"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXOpen"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXOpen"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXOpen\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-browse ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractFiles"; ValueType: string; ValueName: ""; ValueData: "Extract Files..."; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractFiles"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractFiles"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractFiles\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract-files ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractHere"; ValueType: string; ValueName: ""; ValueData: "Extract Here"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractHere"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractHere"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractHere\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract-here ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractTo"; ValueType: string; ValueName: ""; ValueData: "Extract to Archive Folder"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractTo"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractTo"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.7z\shell\WinZOXExtractTo\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXOpen"; ValueType: string; ValueName: ""; ValueData: "Open with WinZOX"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXOpen"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXOpen"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXOpen\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-browse ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractFiles"; ValueType: string; ValueName: ""; ValueData: "Extract Files..."; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractFiles"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractFiles"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractFiles\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract-files ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractHere"; ValueType: string; ValueName: ""; ValueData: "Extract Here"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractHere"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractHere"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractHere\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract-here ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractTo"; ValueType: string; ValueName: ""; ValueData: "Extract to Archive Folder"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractTo"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractTo"; ValueType: string; ValueName: "Position"; ValueData: "Top"
Root: HKLM; Subkey: "Software\Classes\SystemFileAssociations\.rar\shell\WinZOXExtractTo\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-extract ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToArchive"; ValueType: string; ValueName: ""; ValueData: "Add to archive"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToArchive"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Add to archive"
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToArchive"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToArchive"; ValueType: string; ValueName: "Position"; ValueData: "Bottom"
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToArchive\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-add %*"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToArchive"; ValueType: string; ValueName: ""; ValueData: "Add to archive"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToArchive"; ValueType: string; ValueName: "MUIVerb"; ValueData: "Add to archive"
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToArchive"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToArchive"; ValueType: string; ValueName: "Position"; ValueData: "Bottom"
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToArchive\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-add ""%1"""; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToZox"; ValueType: string; ValueName: ""; ValueData: "Add to .zox"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToZox"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToZox"; ValueType: string; ValueName: "Position"; ValueData: "Bottom"
Root: HKLM; Subkey: "Software\Classes\*\shell\WinZOXAddToZox\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-quick-zox %*"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToZox"; ValueType: string; ValueName: ""; ValueData: "Add to .zox"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToZox"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\winzox-favicon.ico"""
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToZox"; ValueType: string; ValueName: "Position"; ValueData: "Bottom"
Root: HKLM; Subkey: "Software\Classes\Directory\shell\WinZOXAddToZox\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" shell-quick-zox ""%1"""; Flags: uninsdeletekey
