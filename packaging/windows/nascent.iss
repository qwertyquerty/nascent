#ifndef AppVersion
  #error AppVersion must be defined
#endif
#ifndef NumericVersion
  #error NumericVersion must be defined
#endif
#ifndef SourceDir
  #error SourceDir must be defined
#endif
#ifndef OutputDir
  #error OutputDir must be defined
#endif
#ifndef OutputBaseFilename
  #error OutputBaseFilename must be defined
#endif
#ifndef IconFile
  #error IconFile must be defined
#endif

[Setup]
AppId={{C0B50DD6-2EE5-4B10-B974-2E14DF225101}
AppName=Nascent
AppVersion={#AppVersion}
AppVerName=Nascent {#AppVersion}
AppPublisher=qwertyquerty
AppPublisherURL=https://github.com/qwertyquerty/nascent
AppSupportURL=https://github.com/qwertyquerty/nascent/issues
AppUpdatesURL=https://github.com/qwertyquerty/nascent/releases
VersionInfoVersion={#NumericVersion}
PrivilegesRequired=lowest
DefaultDirName={userpf}\Nascent
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\Nascent.exe
UninstallDisplayName=Nascent
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
Type: filesandordirs; Name: "{app}\assets"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\.nascent-update"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\Nascent"; Filename: "{app}\Nascent.exe"
Name: "{userdesktop}\Nascent"; Filename: "{app}\Nascent.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Nascent.exe"; Description: "{cm:LaunchProgram,Nascent}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\assets"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\.nascent-update"
Type: files; Name: "{app}\Nascent.exe"
Type: files; Name: "{app}\README.md"
Type: dirifempty; Name: "{app}"
