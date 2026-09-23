; SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
; SPDX-License-Identifier: GPL-2.0-or-later
;
; Kyty-021: Inno Setup installer for AbdoPS5 (Windows x64)
;
; Usage (from CI or locally):
;   iscc packaging/windows/AbdoPS5.iss
;
; Produces: AbdoPS5-Setup-{version}-x64.exe
;
; The installer:
; - Copies the emulator + launcher + tools + data to Program Files
; - Creates Start Menu shortcuts
; - Creates desktop shortcut (optional)
; - Registers in Add/Remove Programs
; - Associates .pkg files with the launcher (optional)
; - Uninstalls cleanly

#define MyAppName "AbdoPS5"
#define MyAppPublisher "AbdoPS5 Emulator Project"
#define MyAppURL "https://github.com/OGKushhh/AbdoPS5"
#define MyAppExeName "launcher.exe"
#define MyAppVersion "0.1.0"

[Setup]
AppId={{A6B5C3D2-1F2E-4A3B-9C8D-7E6F5A4B3C2D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=LICENSE
OutputDir=.
OutputBaseFilename=AbdoPS5-Setup-{#MyAppVersion}-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate_pkg"; Description: "Associate .pkg files with {#MyAppName}"; GroupDescription: "Other:"

[Files]
; The install directory is the CI output: _Build/windows/install/
; When building locally, copy the build output there first.
Source: "_Build\windows\install\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{#MyAppName} on GitHub"; Filename: "{#MyAppURL}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Associate .pkg files with the launcher
Root: HKCR; Subkey: ".pkg"; ValueType: string; ValueName: ""; ValueData: "AbdoPS5.PKG"; Flags: uninsdeletevalue; Tasks: associate_pkg
Root: HKCR; Subkey: "AbdoPS5.PKG"; ValueType: string; ValueName: ""; ValueData: "PS5 Package File"; Flags: uninsdeletekey; Tasks: associate_pkg
Root: HKCR; Subkey: "AbdoPS5.PKG\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: associate_pkg
Root: HKCR; Subkey: "AbdoPS5.PKG\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: associate_pkg

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\screenshots"
Type: filesandordirs; Name: "{app}\_Shaders"
Type: filesandordirs; Name: "{app}\_Buffers"
