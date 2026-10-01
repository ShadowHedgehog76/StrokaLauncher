; Installateur Windows de Stroka Launcher (Inno Setup 6), généré par GitHub Actions.
; Compilation : ISCC.exe /DAppVersion=1.2.3 /DSourceDir="dist\Stroka Launcher" packaging\windows\installer.iss
; Installation pour l'utilisateur courant (pas de droits administrateur) : le launcher peut ainsi se mettre à jour
; tout seul dans son dossier.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\Stroka Launcher"
#endif

[Setup]
AppId={{908D35FD-1DF3-4E4A-A392-3F3199F98EF2}
AppName=Stroka Launcher
AppVersion={#AppVersion}
AppVerName=Stroka Launcher {#AppVersion}
AppPublisher=Stroka
AppPublisherURL=https://github.com/ShadowHedgehog76/StrokaLauncher
AppSupportURL=https://github.com/ShadowHedgehog76/StrokaLauncher/issues
AppUpdatesURL=https://github.com/ShadowHedgehog76/StrokaLauncher/releases
DefaultDirName={localappdata}\Programs\Stroka Launcher
DefaultGroupName=Stroka Launcher
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\..\dist
OutputBaseFilename=StrokaLauncher-Setup-Windows-x64
SetupIconFile=stroka.ico
UninstallDisplayIcon={app}\StrokaLauncher.exe
UninstallDisplayName=Stroka Launcher
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "Créer un raccourci sur le Bureau"; GroupDescription: "Raccourcis :"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Stroka Launcher"; Filename: "{app}\StrokaLauncher.exe"
Name: "{autodesktop}\Stroka Launcher"; Filename: "{app}\StrokaLauncher.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\StrokaLauncher.exe"; Description: "Lancer Stroka Launcher"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; restes des mises à jour automatiques (fichiers remplacés pendant que le launcher tournait)
Type: files; Name: "{app}\*.old"
Type: filesandordirs; Name: "{app}\.stroka-update"

[Code]
{ Les données (comptes, packs, mondes) restent dans %APPDATA%\StrokaLauncher : proposées à la suppression }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Data: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    Data := ExpandConstant('{userappdata}\StrokaLauncher');
    if DirExists(Data) then
      if MsgBox('Supprimer aussi les données du launcher (compte, packs installés, mondes solo) ?' + #13#10 + Data,
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(Data, True, True, True);
  end;
end;
