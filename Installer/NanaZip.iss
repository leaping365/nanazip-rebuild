; Traditional installer. The context menu comes from the bundled MSIX.
; A normal PC has no developer mode, no signing certificate, and no
; AppModelUnlock values, so the package must be trusted and registered
; here. Shortcuts are created only after that package is installed.

#define AppName "NanaZip"
#ifndef AppVersion
  #define AppVersion "1.0.1.21"
#endif
#ifndef PayloadDir
  #error Run BuildInstaller.ps1 to prepare PayloadDir.
#endif
#ifndef PackagePath
  #error Run BuildInstaller.ps1 to prepare PackagePath.
#endif
#define AppPublisher "M2-Team"
#define AppURL "https://github.com/M2Team/NanaZip"
#define AppUserModelId "NanaZip.LocalDebug.ContextMenu_gt7n2906vqsf8!NanaZip"

[Setup]
AppId={{B7E4A1C8-6D32-4F90-9A15-2C8E5D0B7F63}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription=NanaZip 安装程序
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir=..\Output
OutputBaseFilename=NanaZip-{#AppVersion}-Setup
SetupIconFile=NanaZip.ico
WizardSmallImageFile=WizardLogo.bmp
UninstallDisplayIcon={app}\NanaZip.ico
WizardStyle=modern
SolidCompression=yes
Compression=lzma2
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=no
RestartApplications=no

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackagePath}"; DestDir: "{app}"; DestName: "NanaZip.LocalDebug.Full.msix"; Flags: ignoreversion
Source: "NanaZip.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "NanaZip.LocalDebug.cer"; DestDir: "{app}"; Flags: ignoreversion
Source: "InstallTrust.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "RegisterContextMenu.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "UnregisterContextMenu.ps1"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
Type: files; Name: "{autodesktop}\{#AppName}.lnk"
Type: files; Name: "{group}\{#AppName}.lnk"
Type: files; Name: "{commonappdata}\NanaZip\trust-result.txt"
Type: files; Name: "{commonappdata}\NanaZip\appx-result.txt"

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\UnregisterContextMenu.ps1"""; Flags: runhidden waituntilterminated; RunOnceId: "RemoveNanaZipContextMenu"

[Code]
function RunHidden(const FileName, Params: string; AsOriginalUser: Boolean; var ResultCode: Integer): Boolean;
begin
  ResultCode := -1;
  if AsOriginalUser then
    Result := ExecAsOriginalUser(FileName, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
  else
    Result := Exec(FileName, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function ReadResult(const Path: string): string;
var
  S: AnsiString;
begin
  Result := '';
  if LoadStringFromFile(Path, S) then
    Result := Trim(string(S));
end;

function ResultOk(const Detail: string): Boolean;
begin
  Result := (Length(Detail) >= 2) and (CompareText(Copy(Detail, 1, 2), 'OK') = 0);
end;

procedure CreateAppShortcut(const LinkPath: string);
var
  Shell, Link: Variant;
begin
  ForceDirectories(ExtractFilePath(LinkPath));
  Shell := CreateOleObject('WScript.Shell');
  Link := Shell.CreateShortcut(LinkPath);
  Link.TargetPath := ExpandConstant('{win}\explorer.exe');
  Link.Arguments := 'shell:AppsFolder\{#AppUserModelId}';
  Link.WorkingDirectory := ExpandConstant('{app}');
  Link.IconLocation := ExpandConstant('{app}\NanaZip.ico') + ',0';
  Link.Description := 'NanaZip';
  Link.Save;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  PS, TrustArgs, AppxArgs, TrustLog, AppxLog, Detail: string;
  Code: Integer;
  Ok: Boolean;
begin
  if CurStep <> ssPostInstall then
    Exit;

  PS := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  TrustLog := ExpandConstant('{commonappdata}\NanaZip\trust-result.txt');
  AppxLog := ExpandConstant('{commonappdata}\NanaZip\appx-result.txt');
  ForceDirectories(ExpandConstant('{commonappdata}\NanaZip'));
  WizardForm.StatusLabel.Caption := '正在信任签名并打开旁加载...';
  WizardForm.Update;

  TrustArgs := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\InstallTrust.ps1') + '" -ResultPath "' + TrustLog + '"';
  Ok := RunHidden(PS, TrustArgs, False, Code);
  Detail := ReadResult(TrustLog);
  if (not Ok) or (Code <> 0) or (not ResultOk(Detail)) then
  begin
    if Detail = '' then
      Detail := '信任步骤没有返回结果，退出码 ' + IntToStr(Code);
    MsgBox('NanaZip 未能准备旁加载，没有创建快捷方式。' + #13#10 + #13#10 + Detail, mbError, MB_OK);
    Abort;
  end;

  WizardForm.StatusLabel.Caption := '正在安装应用包...';
  WizardForm.Update;
  AppxArgs := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\RegisterContextMenu.ps1') + '" -ResultPath "' + AppxLog + '" -ExpectedVersion "{#AppVersion}"';
  Ok := RunHidden(PS, AppxArgs, True, Code);
  Detail := ReadResult(AppxLog);
  if (not Ok) or (Code <> 0) or (not ResultOk(Detail)) then
  begin
    if Detail = '' then
      Detail := 'Add-AppxPackage 没有返回结果，退出码 ' + IntToStr(Code);
    MsgBox('NanaZip 应用包没有安装成功，没有创建桌面和开始菜单快捷方式。' + #13#10 + #13#10 + Detail, mbError, MB_OK);
    Abort;
  end;

  WizardForm.StatusLabel.Caption := '正在创建快捷方式...';
  WizardForm.Update;
  CreateAppShortcut(ExpandConstant('{autodesktop}\{#AppName}.lnk'));
  CreateAppShortcut(ExpandConstant('{group}\{#AppName}.lnk'));
end;
