; Inno Setup script for the Windows installer.
; Built by CI:  ISCC /DAppVersion=1.0.0 /DBuildDir=..\..\build\Release installer\windows\SlideCapture.iss
; Installs per user (no admin rights) into Documents\Resolume Arena|Avenue\Extra Effects.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\Release"
#endif

[Setup]
AppId={{6C1B2E7A-4F3D-4C8B-9A61-2D5E8F0B7C34}
AppName=SlideCapture for Resolume
AppVersion={#AppVersion}
AppVerName=SlideCapture for Resolume {#AppVersion}
AppPublisher=SlideCapture
AppComments=SC Screen Capture and SC Presentation FFGL plugins for Resolume 7.8+
DefaultDirName={localappdata}\SlideCapture
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir=..\..\dist
OutputBaseFilename=SlideCapture-Setup-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Asks to close Resolume if it still has the plugin files open (upgrade / uninstall).
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=SlideCapture for Resolume

[Messages]
WelcomeLabel2=This installs the SC Screen Capture and SC Presentation sources into the Extra Effects folder of Resolume Arena and/or Avenue (7.8 or newer).%n%nPlease close Resolume before continuing.

[Files]
Source: "{#BuildDir}\SCScreenCapture.dll"; DestDir: "{userdocs}\Resolume Arena\Extra Effects"; Check: InstallArena; Flags: ignoreversion
Source: "{#BuildDir}\SCPresentation.dll";  DestDir: "{userdocs}\Resolume Arena\Extra Effects"; Check: InstallArena; Flags: ignoreversion
Source: "{#BuildDir}\SCScreenCapture.dll"; DestDir: "{userdocs}\Resolume Avenue\Extra Effects"; Check: InstallAvenue; Flags: ignoreversion
Source: "{#BuildDir}\SCPresentation.dll";  DestDir: "{userdocs}\Resolume Avenue\Extra Effects"; Check: InstallAvenue; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
; conversion cache (PPT -> PDF, Google Slides downloads)
Type: filesandordirs; Name: "{localappdata}\Temp\ResolumeSlideCapture"

[Code]
function HasResolume(Name: String): Boolean;
begin
  Result := DirExists(ExpandConstant('{userdocs}\') + Name);
end;

function InstallAvenue: Boolean;
begin
  Result := HasResolume('Resolume Avenue');
end;

function InstallArena: Boolean;
begin
  { Arena is the default when no Resolume folder exists yet }
  Result := HasResolume('Resolume Arena') or not HasResolume('Resolume Avenue');
end;
