; Sol per-user installer (Inno Setup 7, pinned in cmake/innosetup.lock.json).
; Built by scripts/build_installer.ps1, which stages the app and passes:
;   StageDir    staged app files (sol.exe already carries Red's icon)
;   AppVersion  a.b.c
;   ImageDir    wizard images (scripts/make_icons.py --installer-images)
;   OutputDir   where the setup exe is written
;   TestVariant (optional, build_installer.ps1 -TestVariant) builds a separate
;   app identity, folder and profile so installer tests never touch the real
;   install or profile.
; See docs/installer.md.

#ifndef StageDir
  #error StageDir must be defined (use scripts/build_installer.ps1)
#endif
#ifndef AppVersion
  #error AppVersion must be defined
#endif
#ifndef ImageDir
  #error ImageDir must be defined
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
; %LOCALAPPDATA%\<InstallRoot> holds Application\ (this installer) and
; User Data\ (the profile sol.exe uses by default).
#ifdef TestVariant
  #define AppIdGuid "975FE1FC-44F7-413D-8800-CF47AFA21161"
  #define AppName "Sol Installer Test"
  #define InstallRoot "SolInstallerTest"
  #define AppUserModel "SolInstallerTest"
  #define ProfileArgs "--profile-dir=""{localappdata}\SolInstallerTest\User Data"""
#else
  ; Never change the production AppId: it identifies the install for
  ; upgrades and its "Apps & features" entry. Sol has its own (Red's was
  ; 8BF0E7E3-A5A2-4B5E-A7DA-0E0F044B07F6); Red's folder is cleaned up below.
  #define AppIdGuid "55D75F28-1394-4E21-88D0-D3BA0D757491"
  #define AppName "Sol"
  #define InstallRoot "Sol"
  ; Matches kBrowserAppId (windows/browser_identity.h).
  #define AppUserModel "Sol.Browser"
  #define ProfileArgs ""
#endif
#define LaunchArgs Trim("--workspace " + ProfileArgs)
; Inno parameter values double any embedded quotes.
#define InnoLaunchArgs StringChange(LaunchArgs, '"', '""')

[Setup]
AppId={{{#AppIdGuid}}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Sol Browser
#ifndef TestVariant
ChangesAssociations=yes
#endif
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
; Per-user, no elevation. The fixed folder keeps Windows Firewall and other
; per-path state stable across upgrades (same path as scripts/install_local.ps1).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
DefaultDirName={localappdata}\{#InstallRoot}\Application
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableWelcomePage=no
; The wizard follows the Windows display language (Russian or English).
ShowLanguageDialog=no
DefaultGroupName={#AppName}
UsePreviousAppDir=no
; Red is closed by our own WM_CLOSE logic below; never let Restart Manager
; shut down or terminate processes.
CloseApplications=no
RestartApplications=no
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\sol.exe
SetupIconFile=..\apps\desktop\resources\red.ico
WizardStyle=modern dynamic
WizardSmallImageFile={#ImageDir}\wizard-small-1x.png,{#ImageDir}\wizard-small-2x.png
WizardImageFile={#ImageDir}\wizard-1x.png,{#ImageDir}\wizard-2x.png
WizardImageFileDynamicDark={#ImageDir}\wizard-dark-1x.png,{#ImageDir}\wizard-dark-2x.png
OutputDir={#OutputDir}
OutputBaseFilename={#StringChange(AppName, " ", "")}-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=4

[Languages]
; The first entry is the fallback for other display languages.
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
ru.LaunchRed=Запустить {#AppName}
en.LaunchRed=Launch {#AppName}
ru.RedStillRunning={#AppName} всё ещё открыт. Закройте его и нажмите «Повтор».
en.RedStillRunning={#AppName} is still running. Close it and click Retry.
ru.RedNotClosed=Не удалось закрыть {#AppName}. Закройте браузер и запустите программу ещё раз.
en.RedNotClosed=Could not close {#AppName}. Close the browser and run this program again.
ru.RemoveProfile=Удалить также профиль {#AppName} (вкладки, история, расширения, пароли и настройки)?%n%nЕсли нажать «Нет», данные останутся и вернутся при повторной установке.
en.RemoveProfile=Also delete your {#AppName} profile (tabs, history, extensions, passwords and settings)?%n%nIf you choose No, your data stays and returns when you reinstall.
ru.AclFailed=Не удалось выдать песочнице доступ к папке программы (icacls, код %1).
en.AclFailed=Could not grant the sandbox access to the program folder (icacls exit code %1).

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Mirror semantics on upgrade: remove the previous build's app files (also
; ones copied by scripts/install_local.ps1) without touching anything else.
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\*.pak"
Type: files; Name: "{app}\*.bin"
Type: files; Name: "{app}\*.json"
Type: files; Name: "{app}\icudtl.dat"
Type: files; Name: "{app}\sol.exe"
Type: files; Name: "{app}\*_test.exe"
Type: filesandordirs; Name: "{app}\locales"
Type: filesandordirs; Name: "{app}\ui"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\{#AppName}"; Filename: "{app}\sol.exe"; Parameters: "{#InnoLaunchArgs}"; WorkingDir: "{app}"; Comment: "{#AppName}"; AppUserModelID: "{#AppUserModel}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\sol.exe"; Parameters: "{#InnoLaunchArgs}"; WorkingDir: "{app}"; Comment: "{#AppName}"; Tasks: desktopicon; AppUserModelID: "{#AppUserModel}"

#ifndef TestVariant
; Advertise Red as a candidate; Windows Settings owns the user's effective choice.
; Never write UserChoice or the defaults of http/https/.html/.htm.
[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\sol.exe"; ValueType: string; ValueData: "{app}\sol.exe"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\sol.exe"; ValueType: string; ValueName: "Path"; ValueData: "{app}"
Root: HKCU; Subkey: "Software\Classes\Applications\sol.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Sol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\sol.exe\shell\open\command"; ValueType: string; ValueData: """{app}\sol.exe"" --workspace -- ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\sol.exe\SupportedTypes"; ValueType: string; ValueName: ".htm"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\sol.exe\SupportedTypes"; ValueType: string; ValueName: ".html"; ValueData: ""
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol"; ValueType: string; ValueData: "Sol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\DefaultIcon"; ValueType: string; ValueData: """{app}\sol.exe"",0"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\shell\open\command"; ValueType: string; ValueData: """{app}\sol.exe"" --workspace"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\InstallInfo"; ValueType: string; ValueName: "ReinstallCommand"; ValueData: """{app}\sol.exe"" --make-default-browser"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\InstallInfo"; ValueType: dword; ValueName: "IconsVisible"; ValueData: "1"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Sol"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Browse the web with Sol."
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: """{app}\sol.exe"",0"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\Capabilities\Startmenu"; ValueType: string; ValueName: "StartMenuInternet"; ValueData: "Sol"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\Capabilities\URLAssociations"; ValueType: string; ValueName: "http"; ValueData: "SolBrowserURL"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\Capabilities\URLAssociations"; ValueType: string; ValueName: "https"; ValueData: "SolBrowserURL"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\Capabilities\FileAssociations"; ValueType: string; ValueName: ".htm"; ValueData: "SolBrowserHTML"
Root: HKCU; Subkey: "Software\Clients\StartMenuInternet\Sol\Capabilities\FileAssociations"; ValueType: string; ValueName: ".html"; ValueData: "SolBrowserHTML"
Root: HKCU; Subkey: "Software\Classes\SolBrowserURL"; ValueType: string; ValueData: "Sol URL"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SolBrowserURL"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\SolBrowserURL"; ValueType: string; ValueName: "AppUserModelId"; ValueData: "{#AppUserModel}"
Root: HKCU; Subkey: "Software\Classes\SolBrowserURL\Application"; ValueType: string; ValueName: "AppUserModelId"; ValueData: "{#AppUserModel}"
Root: HKCU; Subkey: "Software\Classes\SolBrowserURL\DefaultIcon"; ValueType: string; ValueData: """{app}\sol.exe"",0"
Root: HKCU; Subkey: "Software\Classes\SolBrowserURL\shell\open\command"; ValueType: string; ValueData: """{app}\sol.exe"" --workspace -- ""%1"""
Root: HKCU; Subkey: "Software\Classes\SolBrowserURL\Application"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Sol"
Root: HKCU; Subkey: "Software\Classes\SolBrowserURL\Application"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: """{app}\sol.exe"",0"
Root: HKCU; Subkey: "Software\Classes\SolBrowserHTML"; ValueType: string; ValueData: "Sol HTML Document"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\SolBrowserHTML"; ValueType: string; ValueName: "AppUserModelId"; ValueData: "{#AppUserModel}"
Root: HKCU; Subkey: "Software\Classes\SolBrowserHTML\Application"; ValueType: string; ValueName: "AppUserModelId"; ValueData: "{#AppUserModel}"
Root: HKCU; Subkey: "Software\Classes\SolBrowserHTML\DefaultIcon"; ValueType: string; ValueData: """{app}\sol.exe"",0"
Root: HKCU; Subkey: "Software\Classes\SolBrowserHTML\shell\open\command"; ValueType: string; ValueData: """{app}\sol.exe"" --workspace -- ""%1"""
Root: HKCU; Subkey: "Software\Classes\SolBrowserHTML\Application"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Sol"
Root: HKCU; Subkey: "Software\Classes\SolBrowserHTML\Application"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: """{app}\sol.exe"",0"
Root: HKCU; Subkey: "Software\Classes\.htm\OpenWithProgids"; ValueType: string; ValueName: "SolBrowserHTML"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\Classes\.html\OpenWithProgids"; ValueType: string; ValueName: "SolBrowserHTML"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Sol"; ValueData: "Software\Clients\StartMenuInternet\Sol\Capabilities"; Flags: uninsdeletevalue
#endif

[Run]
Filename: "{app}\sol.exe"; Parameters: "{#InnoLaunchArgs}"; WorkingDir: "{app}"; Description: "{cm:LaunchRed}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Chromium writes these next to the exe at runtime.
Type: files; Name: "{app}\debug.log"

[Code]
const
  WM_CLOSE = $0010;
  GW_OWNER = 4;
  CloseTimeoutMs = 30000;

var
  RedPids: array of DWORD;
  ClosePosted: Integer;

function EnumWindows(EnumFunc: NativeInt; Param: NativeInt): BOOL;
  external 'EnumWindows@user32.dll stdcall';
function GetWindowThreadProcessId(Wnd: HWND; var ProcessId: DWORD): DWORD;
  external 'GetWindowThreadProcessId@user32.dll stdcall';
function IsWindowVisible(Wnd: HWND): Integer;
  external 'IsWindowVisible@user32.dll stdcall';
function GetWindow(Wnd: HWND; Cmd: UINT): HWND;
  external 'GetWindow@user32.dll stdcall';

function RedExe: String;
begin
  Result := ExpandConstant('{app}\sol.exe');
end;

{ Process ids of sol.exe running from exactly this install's exe.
  Processes started from any other path (dev builds) are never touched. }
procedure FindRedProcesses;
var
  Locator, Service, Found, Item: Variant;
  I: Integer;
  Path: String;
begin
  SetArrayLength(RedPids, 0);
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Found := Service.ExecQuery('SELECT ProcessId, ExecutablePath FROM Win32_Process WHERE Name = ''sol.exe''');
    for I := 0 to Found.Count - 1 do
    begin
      Item := Found.ItemIndex(I);
      if not VarIsNull(Item.ExecutablePath) then
      begin
        Path := Item.ExecutablePath;
        if CompareText(Path, RedExe) = 0 then
        begin
          SetArrayLength(RedPids, GetArrayLength(RedPids) + 1);
          RedPids[GetArrayLength(RedPids) - 1] := Item.ProcessId;
        end;
      end;
    end;
  except
    Log('Process query failed: ' + GetExceptionMessage);
  end;
end;

function IsRedPid(Pid: DWORD): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to GetArrayLength(RedPids) - 1 do
    if RedPids[I] = Pid then
    begin
      Result := True;
      Exit;
    end;
end;

{ Asks each visible, unowned top-level window of this install's browser to
  close, exactly like clicking its close button. Red runs its normal shutdown
  (session and tree state are flushed); nothing is ever terminated. }
function CloseWindowProc(Wnd: HWND; Param: NativeInt): BOOL;
var
  Pid: DWORD;
begin
  Result := True;
  Pid := 0;
  GetWindowThreadProcessId(Wnd, Pid);
  if IsRedPid(Pid) and (IsWindowVisible(Wnd) <> 0) and (GetWindow(Wnd, GW_OWNER) = 0) then
  begin
    PostMessage(Wnd, WM_CLOSE, 0, 0);
    ClosePosted := ClosePosted + 1;
    Log(Format('Posted WM_CLOSE to window %d of PID %d', [Wnd, Pid]));
  end;
end;

{ True when no process runs this install's exe any more. }
function CloseRed: Boolean;
var
  Waited: Integer;
begin
  FindRedProcesses;
  Result := GetArrayLength(RedPids) = 0;
  if Result then
    Exit;
  Log(Format('%d Sol process(es) running from %s', [GetArrayLength(RedPids), RedExe]));
  ClosePosted := 0;
  EnumWindows(CreateCallback(@CloseWindowProc), 0);
  Waited := 0;
  while Waited < CloseTimeoutMs do
  begin
    Sleep(250);
    Waited := Waited + 250;
    FindRedProcesses;
    if GetArrayLength(RedPids) = 0 then
    begin
      Log(Format('Sol exited after %d ms', [Waited]));
      Result := True;
      Exit;
    end;
  end;
  Log('Sol did not exit in time');
end;

function EnsureRedClosed: Boolean;
begin
  Result := CloseRed;
  while not Result do
  begin
    if SuppressibleMsgBox(CustomMessage('RedStillRunning'), mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then
      Exit;
    Result := CloseRed;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not EnsureRedClosed then
    Result := CustomMessage('RedNotClosed');
end;

#ifndef TestVariant
{ Red -> Sol: Sol moves Red's profile on its first start; Red's program files,
  Start-menu entry and "Apps" entry are left over. Red's profile folder is
  never touched here.
  A Red-Setup install is removed by Red's own uninstaller, run silently: it
  closes a running Red gracefully, removes Red's registration and its "Apps"
  entry, and keeps the profile (Red's silent uninstall deletes it only with
  /PURGEPROFILE=1). A copied Red (scripts/install_local.ps1) has no
  uninstaller: its folder is deleted, only when no Red process runs. }
const
  RedUninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{8BF0E7E3-A5A2-4B5E-A7DA-0E0F044B07F6}_is1';

procedure UninstallRed;
var
  Command: String;
  Code, Waited: Integer;
begin
  if not RegQueryStringValue(HKCU, RedUninstallKey, 'UninstallString', Command) or (Command = '') then
    Exit;
  Command := RemoveQuotes(Command);
  if not FileExists(Command) then
  begin
    Log('Red''s uninstaller is missing; removing its stale Apps entry');
    RegDeleteKeyIncludingSubkeys(HKCU, RedUninstallKey);
    Exit;
  end;
  Log('Running Red''s uninstaller ' + Command);
  if not Exec(Command, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code) then
  begin
    Log('Red''s uninstaller could not start');
    Exit;
  end;
  { An Inno uninstaller hands over to a temporary copy of itself and returns
    at once; it is finished when its Apps entry is gone. }
  Waited := 0;
  while RegKeyExists(HKCU, RedUninstallKey) and (Waited < 60000) do
  begin
    Sleep(250);
    Waited := Waited + 250;
  end;
  Log(Format('Red''s uninstaller exited with %d; entry removed: %d', [Code, Ord(not RegKeyExists(HKCU, RedUninstallKey))]));
end;

procedure RemoveRed;
var
  Locator, Service, Found: Variant;
  Folder: String;
begin
  UninstallRed;
  Folder := ExpandConstant('{localappdata}\RedBrowser\Application');
  if not FileExists(Folder + '\red_browser.exe') then
  begin
    RemoveDir(Folder);
    RemoveDir(ExpandConstant('{localappdata}\RedBrowser'));
    DeleteFile(ExpandConstant('{userprograms}\Red.lnk'));
    DeleteFile(ExpandConstant('{userdesktop}\Red.lnk'));
    Exit;
  end;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Found := Service.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''red_browser.exe''');
    if Found.Count > 0 then
    begin
      Log('Red is running; its old program folder is kept');
      Exit;
    end;
  except
    Log('Process query failed; Red''s old program folder is kept');
    Exit;
  end;
  Log('Removing Red''s old program folder ' + Folder);
  DelTree(Folder, True, True, True);
  RemoveDir(ExpandConstant('{localappdata}\RedBrowser'));
  DeleteFile(ExpandConstant('{userprograms}\Red.lnk'));
  DeleteFile(ExpandConstant('{userdesktop}\Red.lnk'));
end;
#endif

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  LegacyCapabilities: String;
begin
  if CurStep = ssPostInstall then
  begin
#ifndef TestVariant
    { Remove only our obsolete mismatched registration name. }
    if RegQueryStringValue(HKCU, 'Software\RegisteredApplications', 'RedBrowser', LegacyCapabilities) and
       (CompareText(LegacyCapabilities, 'Software\Clients\StartMenuInternet\RedBrowser\Capabilities') = 0) then
      RegDeleteValue(HKCU, 'Software\RegisteredApplications', 'RedBrowser');
    RemoveRed;
#endif
    { Same ACL the build applies (cef_macros.cmake SET_LPAC_ACLS): sandboxed
      LPAC processes must read and execute the browser's own files. }
    if not Exec(ExpandConstant('{sys}\icacls.exe'), AddQuotes(ExpandConstant('{app}')) + ' /grant *S-1-15-2-2:(OI)(CI)(RX)',
                '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
      RaiseException(FmtMessage(CustomMessage('AclFailed'), [IntToStr(Code)]));
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := EnsureRedClosed;
  if not Result then
    SuppressibleMsgBox(CustomMessage('RedNotClosed'), mbError, MB_OK, IDOK);
end;

function ProfileDir: String;
begin
  Result := ExpandConstant('{localappdata}\{#InstallRoot}\User Data');
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  RemoveProfile: Boolean;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  { The profile is kept unless the person explicitly asks to delete it: a
    No-by-default prompt, or /PURGEPROFILE for a silent uninstall. }
  RemoveProfile := False;
  if DirExists(ProfileDir) then
  begin
    if UninstallSilent then
      RemoveProfile := ExpandConstant('{param:PURGEPROFILE|0}') = '1'
    else
      RemoveProfile := MsgBox(CustomMessage('RemoveProfile'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  end;
  if RemoveProfile then
  begin
    Log('Deleting profile ' + ProfileDir);
    DelTree(ProfileDir, True, True, True);
  end;
  RemoveDir(ExpandConstant('{app}'));
  RemoveDir(ExpandConstant('{localappdata}\{#InstallRoot}'));
end;
