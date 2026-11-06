; The Ng DAW installer for Windows, built with Inno Setup 6.
; CI passes the version and the folders, e.g.
;   iscc /DAppVersion=1.0.0 /DAppExe=path\to\Ng DAW.exe /DMcpExe=path\to\NgDawMcp.exe /DOutputDir=installer NgDaw.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppExe
  #define AppExe "..\..\build\NgDaw_artefacts\Release\Ng DAW.exe"
#endif
#ifndef McpExe
  #define McpExe "..\..\build\NgDawMcp_artefacts\Release\NgDawMcp.exe"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\installer"
#endif

[Setup]
AppId={{6E3F1A52-8C1B-4D7E-9B0A-4C2D5E7F9A11}
AppName=Ng DAW
AppVersion={#AppVersion}
AppVerName=Ng DAW {#AppVersion}
AppPublisher=Ng Audio
DefaultDirName={autopf}\Ng DAW
DefaultGroupName=Ng DAW
UninstallDisplayIcon={app}\Ng DAW.exe
OutputDir={#OutputDir}
OutputBaseFilename=NgDaw-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "Open .ngdaw projects with Ng DAW"; GroupDescription: "File types:"

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#McpExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Ng DAW"; Filename: "{app}\Ng DAW.exe"
Name: "{group}\Uninstall Ng DAW"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Ng DAW"; Filename: "{app}\Ng DAW.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.ngdaw"; ValueType: string; ValueName: ""; ValueData: "NgDaw.Project"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\NgDaw.Project"; ValueType: string; ValueName: ""; ValueData: "Ng DAW project"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\NgDaw.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Ng DAW.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\NgDaw.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Ng DAW.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\Ng DAW.exe"; Description: "{cm:LaunchProgram,Ng DAW}"; Flags: nowait postinstall skipifsilent
