#if VER < EncodeVer(6, 7, 0)
  #error Inno Setup 6.7 or newer is required
#endif
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
#ifdef IncludeEditor
  #define GameComponent "; Components: game"
#else
  #define GameComponent ""
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
UninstallDisplayIcon={uninstallexe}
UninstallDisplayName=Nascent
Compression=lzma2
SolidCompression=yes
WizardStyle=modern dark includetitlebar hidebevels
WizardBackColor=#070a0e
WizardBackImageFile=wizard\back_596x432.png,wizard\back_994x720.png,wizard\back_1272x922.png
WizardImageFile=
WizardSmallImageFile=wizard\small_58.png,wizard\small_87.png,wizard\small_116.png,wizard\small_145.png
CloseApplications=yes
RestartApplications=no

#ifdef IncludeEditor
[Types]
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "game"; Description: "Nascent"; Types: custom
Name: "editor"; Description: "Nascent Editor (chart editor)"; Types: custom

#endif
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked{#GameComponent}
#ifdef IncludeEditor
Name: "editordesktopicon"; Description: "Create a desktop shortcut for the chart editor"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; Components: editor
#endif

[InstallDelete]
Type: filesandordirs; Name: "{app}\assets"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\.nascent-update"
#ifdef IncludeEditor
Type: files; Name: "{app}\Nascent.exe"; Components: not game
Type: files; Name: "{userprograms}\Nascent.lnk"; Components: not game
Type: files; Name: "{userdesktop}\Nascent.lnk"; Components: not game
Type: files; Name: "{app}\NascentEditor.exe"; Components: not editor
Type: files; Name: "{userprograms}\Nascent Editor.lnk"; Components: not editor
Type: files; Name: "{userdesktop}\Nascent Editor.lnk"; Components: not editor
#endif

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "\Nascent.exe,\NascentEditor.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Nascent.exe"; DestDir: "{app}"; Flags: ignoreversion{#GameComponent}
#ifdef IncludeEditor
Source: "{#SourceDir}\NascentEditor.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: editor
#endif
Source: "..\..\assets\skins\default\display_font.ttf"; Flags: dontcopy

[Icons]
Name: "{userprograms}\Nascent"; Filename: "{app}\Nascent.exe"{#GameComponent}
Name: "{userdesktop}\Nascent"; Filename: "{app}\Nascent.exe"; Tasks: desktopicon{#GameComponent}
#ifdef IncludeEditor
Name: "{userprograms}\Nascent Editor"; Filename: "{app}\NascentEditor.exe"; Components: editor
Name: "{userdesktop}\Nascent Editor"; Filename: "{app}\NascentEditor.exe"; Tasks: editordesktopicon; Components: editor
#endif

[Run]
Filename: "{app}\Nascent.exe"; Description: "{cm:LaunchProgram,Nascent}"; Flags: nowait postinstall skipifsilent{#GameComponent}
#ifdef IncludeEditor
Filename: "{app}\NascentEditor.exe"; Description: "{cm:LaunchProgram,Nascent Editor}"; Flags: nowait postinstall skipifsilent unchecked; Components: editor
#endif

[UninstallDelete]
Type: filesandordirs; Name: "{app}\assets"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\.nascent-update"
Type: files; Name: "{app}\Nascent.exe"
Type: files; Name: "{app}\NascentEditor.exe"
Type: files; Name: "{app}\README.md"
Type: dirifempty; Name: "{app}"

[Code]
const
  FR_PRIVATE = $10;

var
  DisplayFontPath: String;

function AddFontResourceEx(FileName: String; Flags: Cardinal; Reserved: Cardinal): Integer;
  external 'AddFontResourceExW@gdi32.dll stdcall';

function RemoveFontResourceEx(FileName: String; Flags: Cardinal; Reserved: Cardinal): Boolean;
  external 'RemoveFontResourceExW@gdi32.dll stdcall';

procedure UseDisplayFont(Heading: TNewStaticText);
begin
  Heading.Font.Name := 'Tomorrow';
  Heading.Font.Style := [];
end;

procedure InitializeWizard;
begin
  ExtractTemporaryFile('display_font.ttf');
  DisplayFontPath := ExpandConstant('{tmp}\display_font.ttf');

  if AddFontResourceEx(DisplayFontPath, FR_PRIVATE, 0) > 0 then
  begin
    UseDisplayFont(WizardForm.WelcomeLabel1);
    UseDisplayFont(WizardForm.FinishedHeadingLabel);
    UseDisplayFont(WizardForm.PageNameLabel);
  end
  else
    DisplayFontPath := '';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
#ifdef IncludeEditor
  if (CurPageID = wpSelectComponents) and not WizardIsComponentSelected('game') and not WizardIsComponentSelected('editor') then
  begin
    MsgBox('Choose at least one thing to install.', mbError, MB_OK);
    Result := False;
  end;
#endif
end;

procedure DeinitializeSetup;
begin
  if DisplayFontPath <> '' then
    RemoveFontResourceEx(DisplayFontPath, FR_PRIVATE, 0);
end;
